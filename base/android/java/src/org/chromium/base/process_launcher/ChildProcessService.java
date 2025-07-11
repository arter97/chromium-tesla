// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.process_launcher;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.os.Binder;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcelable;
import android.os.Process;
import android.os.RemoteException;
import android.os.SystemClock;
import android.text.TextUtils;
import android.util.SparseArray;

import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

import org.chromium.base.BaseSwitches;
import org.chromium.base.CommandLine;
import org.chromium.base.ContextUtils;
import org.chromium.base.EarlyTraceEvent;
import org.chromium.base.JavaUtils;
import org.chromium.base.Log;
import org.chromium.base.MemoryPressureLevel;
import org.chromium.base.ThreadUtils;
import org.chromium.base.compat.ApiHelperForN;
import org.chromium.base.library_loader.LibraryLoader;
import org.chromium.base.memory.MemoryPressureMonitor;
import org.chromium.base.metrics.RecordHistogram;

import java.util.List;

import javax.annotation.concurrent.GuardedBy;

/**
 * This is the base class for child services.
 * Pre-Q, and for privileged services, the embedding application should contain ProcessService0,
 * 1, etc subclasses that provide the concrete service entry points, so it can connect to more than
 * one distinct process (i.e. one process per service number, up to limit of N).
 * The embedding application must declare these service instances in the application section
 * of its AndroidManifest.xml, first with some meta-data describing the services:
 *     <meta-data android:name="org.chromium.test_app.SERVICES_NAME"
 *           android:value="org.chromium.test_app.ProcessService"/>
 * and then N entries of the form:
 *     <service android:name="org.chromium.test_app.ProcessServiceX"
 *              android:process=":processX" />
 *
 * Q added bindIsolatedService which supports creating multiple instances from a single manifest
 * declaration for isolated services. In this case, only need to declare instance 0 in the manifest.
 *
 * Subclasses must also provide a delegate in this class constructor. That delegate is responsible
 * for loading native libraries and running the main entry point of the service.
 *
 * This class does not directly inherit from Service because the logic may be used by a Service
 * implementation which cannot directly inherit from this class (e.g. for WebLayer child services).
 */
@JNINamespace("base::android")
public class ChildProcessService {
    private static final String MAIN_THREAD_NAME = "ChildProcessMain";
    private static final String TAG = "ChildProcessService";

    // Only for a check that create is only called once.
    private static boolean sCreateCalled;

    private static int sZygotePid;
    private static long sZygoteStartupTimeMillis;

    private final ChildProcessServiceDelegate mDelegate;
    private final Service mService;
    private final Context mApplicationContext;

    private final Object mBinderLock = new Object();
    private final Object mLibraryInitializedLock = new Object();

    // True if we should enforce that bindToCaller() is called before setupConnection().
    // Only set once in bind(), does not require synchronization.
    private boolean mBindToCallerCheck;

    // PID of the client of this service, set in bindToCaller(), if mBindToCallerCheck is true.
    @GuardedBy("mBinderLock")
    private int mBoundCallingPid;

    @GuardedBy("mBinderLock")
    private String mBoundCallingClazz;

    // This is the native "Main" thread for the renderer / utility process.
    private Thread mMainThread;

    // Parameters received via IPC, only accessed while holding the mMainThread monitor.
    private String[] mCommandLineParams;

    // File descriptors that should be registered natively.
    private FileDescriptorInfo[] mFdInfos;

    @GuardedBy("mLibraryInitializedLock")
    private boolean mLibraryInitialized;

    // Called once the service is bound and all service related member variables have been set.
    // Only set once in bind(), does not require synchronization.
    private boolean mServiceBound;

    // Interface to send notifications to the parent process.
    private IParentProcess mParentProcess;

    public ChildProcessService(
            ChildProcessServiceDelegate delegate, Service service, Context applicationContext) {
        mDelegate = delegate;
        mService = service;
        mApplicationContext = applicationContext;
    }

    // Binder object used by clients for this service.
    private final IChildProcessService.Stub mBinder =
            new IChildProcessService.Stub() {
                // NOTE: Implement any IChildProcessService methods here.
                @Override
                public boolean bindToCaller(String clazz) {
                    assert mBindToCallerCheck;
                    assert mServiceBound;
                    synchronized (mBinderLock) {
                        int callingPid = Binder.getCallingPid();
                        if (mBoundCallingPid == 0 && mBoundCallingClazz == null) {
                            mBoundCallingPid = callingPid;
                            mBoundCallingClazz = clazz;
                        } else if (mBoundCallingPid != callingPid) {
                            Log.e(
                                    TAG,
                                    "Service is already bound by pid %d, cannot bind for pid %d",
                                    mBoundCallingPid,
                                    callingPid);
                            return false;
                        } else if (!TextUtils.equals(mBoundCallingClazz, clazz)) {
                            Log.w(
                                    TAG,
                                    "Service is already bound by %s, cannot bind for %s",
                                    mBoundCallingClazz,
                                    clazz);
                            return false;
                        }
                    }
                    return true;
                }

                @Override
                public ApplicationInfo getAppInfo() {
                    return mApplicationContext.getApplicationInfo();
                }

                @Override
                public void setupConnection(
                        Bundle args, IParentProcess parentProcess, List<IBinder> callbacks)
                        throws RemoteException {
                    assert mServiceBound;
                    synchronized (mBinderLock) {
                        if (mBindToCallerCheck && mBoundCallingPid == 0) {
                            Log.e(TAG, "Service has not been bound with bindToCaller()");
                            parentProcess.finishSetupConnection(-1, 0, 0, null);
                            return;
                        }
                    }

                    int pid = Process.myPid();
                    int zygotePid = 0;
                    long startupTimeMillis = -1;
                    Bundle relroBundle = null;
                    if (LibraryLoader.getInstance().isLoadedByZygote()) {
                        zygotePid = sZygotePid;
                        startupTimeMillis = sZygoteStartupTimeMillis;
                        LibraryLoader.MultiProcessMediator m =
                                LibraryLoader.getInstance().getMediator();
                        m.initInChildProcess();
                        // In a number of cases the app zygote decides not to produce a RELRO FD.
                        // The bundle will tell the receiver to silently ignore it.
                        relroBundle = new Bundle();
                        m.putSharedRelrosToBundle(relroBundle);
                    }
                    // After finishSetupConnection() the parent process will stop accepting
                    // |relroBundle| from this process to ensure that another FD to shared memory
                    // is not sent later.
                    parentProcess.finishSetupConnection(
                            pid, zygotePid, startupTimeMillis, relroBundle);
                    mParentProcess = parentProcess;
                    processConnectionBundle(args, callbacks);
                }

                @Override
                public void forceKill() {
                    assert mServiceBound;
                    Process.killProcess(Process.myPid());
                }

                @Override
                public void onMemoryPressure(@MemoryPressureLevel int pressure) {
                    // This method is called by the host process when the host process reports
                    // pressure to its native side. The key difference between the host process
                    // and its services is that the host process polls memory pressure when it
                    // gets CRITICAL, and periodically invokes pressure listeners until pressure
                    // subsides. (See MemoryPressureMonitor for more info.)
                    //
                    // Services don't poll, so this side-channel is used to notify services about
                    // memory pressure from the host process's POV.
                    //
                    // However, since both host process and services listen to ComponentCallbacks2,
                    // we can't be sure that the host process won't get better signals than their
                    // services.
                    // I.e. we need to watch out for a situation where a service gets CRITICAL, but
                    // the host process gets MODERATE - in this case we need to ignore MODERATE.
                    //
                    // So we're ignoring pressure from the host process if it's better than the last
                    // reported pressure. I.e. the host process can drive pressure up, but it'll go
                    // down only when we the service get a signal through ComponentCallbacks2.
                    ThreadUtils.postOnUiThread(
                            () -> {
                                if (pressure
                                        >= MemoryPressureMonitor.INSTANCE
                                                .getLastReportedPressure()) {
                                    MemoryPressureMonitor.INSTANCE.notifyPressure(pressure);
                                }
                            });
                }

                @Override
                public void dumpProcessStack() {
                    assert mServiceBound;
                    synchronized (mLibraryInitializedLock) {
                        if (!mLibraryInitialized) {
                            Log.e(TAG, "Cannot dump process stack before native is loaded");
                            return;
                        }
                    }
                    ChildProcessServiceJni.get().dumpProcessStack();
                }

                @Override
                public void consumeRelroBundle(Bundle bundle) {
                    mDelegate.consumeRelroBundle(bundle);
                }
            };

    /** Loads Chrome's native libraries and initializes a ChildProcessService. */
    // For sCreateCalled check.
    public void onCreate() {
        Log.i(TAG, "Creating new ChildProcessService pid=%d", Process.myPid());
        if (sCreateCalled) {
            throw new RuntimeException("Illegal child process reuse.");
        }
        sCreateCalled = true;

        // Initialize the context for the application that owns this ChildProcessService object.
        ContextUtils.initApplicationContext(getApplicationContext());

        mDelegate.onServiceCreated();

        // Unlike desktop Linux, on Android we leave the main looper thread to handle Android
        // lifecycle events, and create a separate thread to serve as the main renderer. This
        // affects the thread stack size: instead of getting the kernel default we get the Java
        // default, which can be much smaller. So, explicitly set up a larger stack here.
        long stackSize = ContextUtils.isProcess64Bit() ? 8 * 1024 * 1024 : 4 * 1024 * 1024;

        mMainThread =
                new Thread(
                        /* threadGroup= */ null, this::mainThreadMain, MAIN_THREAD_NAME, stackSize);
        mMainThread.start();
    }

    private void mainThreadMain() {
        try {
            // CommandLine must be initialized before everything else.
            synchronized (mMainThread) {
                while (mCommandLineParams == null) {
                    mMainThread.wait();
                }
            }
            assert mServiceBound;
            CommandLine.init(mCommandLineParams);

            if (CommandLine.getInstance().hasSwitch(BaseSwitches.RENDERER_WAIT_FOR_JAVA_DEBUGGER)) {
                android.os.Debug.waitForDebugger();
            }

            EarlyTraceEvent.onCommandLineAvailableInChildProcess();
            mDelegate.loadNativeLibrary(getApplicationContext());

            synchronized (mLibraryInitializedLock) {
                mLibraryInitialized = true;
                mLibraryInitializedLock.notifyAll();
            }
            synchronized (mMainThread) {
                mMainThread.notifyAll();
                while (mFdInfos == null) {
                    mMainThread.wait();
                }
            }

            SparseArray<String> idsToKeys = mDelegate.getFileDescriptorsIdsToKeys();

            int[] fileIds = new int[mFdInfos.length];
            String[] keys = new String[mFdInfos.length];
            int[] fds = new int[mFdInfos.length];
            long[] regionOffsets = new long[mFdInfos.length];
            long[] regionSizes = new long[mFdInfos.length];
            for (int i = 0; i < mFdInfos.length; i++) {
                FileDescriptorInfo fdInfo = mFdInfos[i];
                String key = idsToKeys != null ? idsToKeys.get(fdInfo.id) : null;
                if (key != null) {
                    keys[i] = key;
                } else {
                    fileIds[i] = fdInfo.id;
                }
                fds[i] = fdInfo.fd.detachFd();
                regionOffsets[i] = fdInfo.offset;
                regionSizes[i] = fdInfo.size;
            }
            ChildProcessServiceJni.get()
                    .registerFileDescriptors(keys, fileIds, fds, regionOffsets, regionSizes);

            mDelegate.onBeforeMain();
        } catch (Throwable e) {
            try {
                mParentProcess.reportExceptionInInit(
                        ChildProcessService.class.getName()
                                + "\n"
                                + android.util.Log.getStackTraceString(e));
            } catch (RemoteException re) {
                Log.e(TAG, "Failed to call reportExceptionInInit.", re);
            }
            JavaUtils.throwUnchecked(e);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            // Record process startup time histograms.
            long startTime = SystemClock.uptimeMillis() - ApiHelperForN.getStartUptimeMillis();
            String baseHistogramName = "Android.ChildProcessStartTimeV2";
            String suffix = ContextUtils.isIsolatedProcess() ? ".Isolated" : ".NotIsolated";
            RecordHistogram.recordMediumTimesHistogram(baseHistogramName + ".All", startTime);
            RecordHistogram.recordMediumTimesHistogram(baseHistogramName + suffix, startTime);
        }

        mDelegate.runMain();
        try {
            mParentProcess.reportCleanExit();
        } catch (RemoteException e) {
            Log.e(TAG, "Failed to call clean exit callback.", e);
        }
        ChildProcessServiceJni.get().exitChildProcess();
    }

    @SuppressWarnings("checkstyle:SystemExitCheck") // Allowed due to http://crbug.com/928521#c16.
    public void onDestroy() {
        Log.i(TAG, "Destroying ChildProcessService pid=%d", Process.myPid());
        System.exit(0);
    }

    /*
     * Returns the communication channel to the service. Note that even if multiple clients were to
     * connect, we should only get one call to this method. So there is no need to synchronize
     * member variables that are only set in this method and accessed from binder methods, as binder
     * methods can't be called until this method returns.
     * @param intent The intent that was used to bind to the service.
     * @return the binder used by the client to setup the connection.
     */
    public IBinder onBind(Intent intent) {
        if (mServiceBound) return mBinder;

        // We call stopSelf() to request that this service be stopped as soon as the client unbinds.
        // Otherwise the system may keep it around and available for a reconnect. The child
        // processes do not currently support reconnect; they must be initialized from scratch every
        // time.
        mService.stopSelf();

        mBindToCallerCheck =
                intent.getBooleanExtra(ChildProcessConstants.EXTRA_BIND_TO_CALLER, false);
        mServiceBound = true;
        mDelegate.onServiceBound(intent);

        String packageName =
                intent.getStringExtra(ChildProcessConstants.EXTRA_BROWSER_PACKAGE_NAME);
        if (packageName == null) {
            packageName = getApplicationContext().getApplicationInfo().packageName;
        }
        // Don't block bind() with any extra work, post it to the application thread instead.
        final String preloadPackageName = packageName;
        new Handler(Looper.getMainLooper())
                .post(() -> mDelegate.preloadNativeLibrary(preloadPackageName));
        return mBinder;
    }

    /** This will be called from the zygote on startup. */
    public static void setZygoteInfo(int zygotePid, long zygoteStartupTimeMillis) {
        sZygotePid = zygotePid;
        sZygoteStartupTimeMillis = zygoteStartupTimeMillis;
    }

    private void processConnectionBundle(Bundle bundle, List<IBinder> clientInterfaces) {
        // Required to unparcel FileDescriptorInfo.
        ClassLoader classLoader = getApplicationContext().getClassLoader();
        bundle.setClassLoader(classLoader);
        synchronized (mMainThread) {
            if (mCommandLineParams == null) {
                mCommandLineParams =
                        bundle.getStringArray(ChildProcessConstants.EXTRA_COMMAND_LINE);
                mMainThread.notifyAll();
            }
            // We must have received the command line by now
            assert mCommandLineParams != null;
            Parcelable[] fdInfosAsParcelable =
                    bundle.getParcelableArray(ChildProcessConstants.EXTRA_FILES);
            if (fdInfosAsParcelable != null) {
                // For why this arraycopy is necessary:
                // http://stackoverflow.com/questions/8745893/i-dont-get-why-this-classcastexception-occurs
                mFdInfos = new FileDescriptorInfo[fdInfosAsParcelable.length];
                System.arraycopy(fdInfosAsParcelable, 0, mFdInfos, 0, fdInfosAsParcelable.length);
            }
            mDelegate.onConnectionSetup(bundle, clientInterfaces);
            mMainThread.notifyAll();
        }
    }

    private Context getApplicationContext() {
        return mApplicationContext;
    }

    @NativeMethods
    interface Natives {
        /**
         * Helper for registering FileDescriptorInfo objects with GlobalFileDescriptors or
         * FileDescriptorStore.
         * This includes the IPC channel, the crash dump signals and resource related
         * files.
         */
        void registerFileDescriptors(String[] keys, int[] id, int[] fd, long[] offset, long[] size);

        /** Force the child process to exit. */
        void exitChildProcess();

        /** Dumps the child process stack without crashing it. */
        void dumpProcessStack();
    }
}
