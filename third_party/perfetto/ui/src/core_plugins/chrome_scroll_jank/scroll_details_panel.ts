// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

import m from 'mithril';

import {duration, Time, time} from '../../base/time';
import {exists} from '../../base/utils';
import {raf} from '../../core/raf_scheduler';
import {BottomTab, NewBottomTabArgs} from '../../frontend/bottom_tab';
import {GenericSliceDetailsTabConfig} from '../../frontend/generic_slice_details_tab';
import {sqlValueToString} from '../../frontend/sql_utils';
import {
  ColumnDescriptor,
  numberColumn,
  Table,
  TableData,
  widgetColumn,
} from '../../frontend/tables/table';
import {DurationWidget} from '../../frontend/widgets/duration';
import {Timestamp} from '../../frontend/widgets/timestamp';
import {LONG, NUM, STR} from '../../trace_processor/query_result';
import {DetailsShell} from '../../widgets/details_shell';
import {GridLayout, GridLayoutColumn} from '../../widgets/grid_layout';
import {Section} from '../../widgets/section';
import {SqlRef} from '../../widgets/sql_ref';
import {MultiParagraphText, TextParagraph} from '../../widgets/text_paragraph';
import {dictToTreeNodes, Tree} from '../../widgets/tree';

import {
  buildScrollOffsetsGraph,
  getInputScrollDeltas,
  getJankIntervals,
  getPresentedScrollDeltas,
} from './scroll_delta_graph';
import {
  getScrollJankSlices,
  getSliceForTrack,
  ScrollJankSlice,
} from './scroll_jank_slice';
import {ScrollJankV3TrackKind} from './common';

interface Data {
  // Scroll ID.
  id: number;
  // Timestamp of the beginning of this slice in nanoseconds.
  ts: time;
  // DurationWidget of this slice in nanoseconds.
  dur: duration;
}

interface Metrics {
  inputEventCount?: number;
  frameCount?: number;
  presentedFrameCount?: number;
  jankyFrameCount?: number;
  jankyFramePercent?: number;
  missedVsyncs?: number;
  startOffset?: number;
  endOffset?: number;
  totalPixelsScrolled?: number;
}

interface JankSliceDetails {
  cause: string;
  jankSlice: ScrollJankSlice;
  delayDur: duration;
  delayVsync: number;
}

export class ScrollDetailsPanel extends BottomTab<GenericSliceDetailsTabConfig> {
  static readonly kind = 'org.perfetto.ScrollDetailsPanel';
  loaded = false;
  data: Data | undefined;
  metrics: Metrics = {};
  orderedJankSlices: JankSliceDetails[] = [];
  scrollDeltas: m.Child;

  static create(
    args: NewBottomTabArgs<GenericSliceDetailsTabConfig>,
  ): ScrollDetailsPanel {
    return new ScrollDetailsPanel(args);
  }

  constructor(args: NewBottomTabArgs<GenericSliceDetailsTabConfig>) {
    super(args);
    this.loadData();
  }

  private async loadData() {
    const queryResult = await this.engine.query(`
      WITH scrolls AS (
        SELECT
          id,
          IFNULL(gesture_scroll_begin_ts, ts) AS start_ts,
          CASE
            WHEN gesture_scroll_end_ts IS NOT NULL THEN gesture_scroll_end_ts
            WHEN gesture_scroll_begin_ts IS NOT NULL 
              THEN gesture_scroll_begin_ts + dur
            ELSE ts + dur
          END AS end_ts
        FROM chrome_scrolls WHERE id = ${this.config.id})
      SELECT
        id,
        start_ts AS ts,
        end_ts - start_ts AS dur
      FROM scrolls`);

    const iter = queryResult.firstRow({
      id: NUM,
      ts: LONG,
      dur: LONG,
    });
    this.data = {
      id: iter.id,
      ts: Time.fromRaw(iter.ts),
      dur: iter.dur,
    };

    await this.loadMetrics();
    this.loaded = true;
    raf.scheduleFullRedraw();
  }

  private async loadMetrics() {
    await this.loadInputEventCount();
    await this.loadFrameStats();
    await this.loadDelayData();
    await this.loadScrollOffsets();
  }

  private async loadInputEventCount() {
    if (exists(this.data)) {
      const queryResult = await this.engine.query(`
        SELECT
          COUNT(*) AS inputEventCount
        FROM slice s
        WHERE s.name = "EventLatency"
          AND EXTRACT_ARG(arg_set_id, 'event_latency.event_type') = 'TOUCH_MOVED'
          AND s.ts >= ${this.data.ts}
          AND s.ts + s.dur <= ${this.data.ts + this.data.dur}
      `);

      const iter = queryResult.firstRow({
        inputEventCount: NUM,
      });

      this.metrics.inputEventCount = iter.inputEventCount;
    }
  }

  private async loadFrameStats() {
    if (exists(this.data)) {
      const queryResult = await this.engine.query(`
        SELECT
          IFNULL(frame_count, 0) AS frameCount,
          IFNULL(missed_vsyncs, 0) AS missedVsyncs,
          IFNULL(presented_frame_count, 0) AS presentedFrameCount,
          IFNULL(janky_frame_count, 0) AS jankyFrameCount,
          ROUND(IFNULL(janky_frame_percent, 0), 2) AS jankyFramePercent
        FROM chrome_scroll_stats
        WHERE scroll_id = ${this.data.id}
      `);
      const iter = queryResult.iter({
        frameCount: NUM,
        missedVsyncs: NUM,
        presentedFrameCount: NUM,
        jankyFrameCount: NUM,
        jankyFramePercent: NUM,
      });

      for (; iter.valid(); iter.next()) {
        this.metrics.frameCount = iter.frameCount;
        this.metrics.missedVsyncs = iter.missedVsyncs;
        this.metrics.presentedFrameCount = iter.presentedFrameCount;
        this.metrics.jankyFrameCount = iter.jankyFrameCount;
        this.metrics.jankyFramePercent = iter.jankyFramePercent;
        return;
      }
    }
  }

  private async loadDelayData() {
    if (exists(this.data)) {
      const queryResult = await this.engine.query(`
        SELECT
          IFNULL(sub_cause_of_jank, IFNULL(cause_of_jank, 'Unknown')) AS cause,
          IFNULL(event_latency_id, 0) AS eventLatencyId,
          IFNULL(dur, 0) AS delayDur,
          IFNULL(delayed_frame_count, 0) AS delayVsync
        FROM chrome_janky_frame_presentation_intervals s
        WHERE s.ts >= ${this.data.ts}
          AND s.ts + s.dur <= ${this.data.ts + this.data.dur}
        ORDER by delayDur DESC;
      `);

      const iter = queryResult.iter({
        cause: STR,
        eventLatencyId: NUM,
        delayDur: LONG,
        delayVsync: NUM,
      });

      for (; iter.valid(); iter.next()) {
        if (iter.delayDur <= 0) {
          break;
        }
        const jankSlices = await getScrollJankSlices(
          this.engine,
          iter.eventLatencyId,
        );

        this.orderedJankSlices.push({
          cause: iter.cause,
          jankSlice: jankSlices[0],
          delayDur: iter.delayDur,
          delayVsync: iter.delayVsync,
        });
      }
    }
  }

  private async loadScrollOffsets() {
    if (exists(this.data)) {
      const inputDeltas = await getInputScrollDeltas(this.engine, this.data.id);
      const presentedDeltas = await getPresentedScrollDeltas(
        this.engine,
        this.data.id,
      );
      const jankIntervals = await getJankIntervals(
        this.engine,
        this.data.ts,
        this.data.dur,
      );
      this.scrollDeltas = buildScrollOffsetsGraph(
        inputDeltas,
        presentedDeltas,
        jankIntervals,
      );

      if (presentedDeltas.length > 0) {
        this.metrics.startOffset = presentedDeltas[0].scrollOffset;
        this.metrics.endOffset =
          presentedDeltas[presentedDeltas.length - 1].scrollOffset;

        let pixelsScrolled = 0;
        for (let i = 0; i < presentedDeltas.length; i++) {
          pixelsScrolled += Math.abs(presentedDeltas[i].scrollDelta);
        }

        if (pixelsScrolled != 0) {
          this.metrics.totalPixelsScrolled = pixelsScrolled;
        }
      }
    }
  }

  private renderMetricsDictionary(): m.Child[] {
    const metrics: {[key: string]: m.Child} = {};
    metrics['Total Finger Input Event Count'] = this.metrics.inputEventCount;
    metrics['Total Vsyncs within Scrolling period'] = this.metrics.frameCount;
    metrics['Total Chrome Presented Frames'] = this.metrics.presentedFrameCount;
    metrics['Total Janky Frames'] = this.metrics.jankyFrameCount;
    metrics['Number of Vsyncs Janky Frames were Delayed by'] =
      this.metrics.missedVsyncs;

    if (this.metrics.jankyFramePercent !== undefined) {
      metrics[
        'Janky Frame Percentage (Total Janky Frames / Total Chrome Presented Frames)'
      ] = `${this.metrics.jankyFramePercent}%`;
    }

    if (this.metrics.startOffset != undefined) {
      metrics['Starting Offset'] = this.metrics.startOffset;
    }

    if (this.metrics.endOffset != undefined) {
      metrics['Ending Offset'] = this.metrics.endOffset;
    }

    if (
      this.metrics.startOffset != undefined &&
      this.metrics.endOffset != undefined
    ) {
      metrics['Net Pixels Scrolled'] = Math.abs(
        this.metrics.endOffset - this.metrics.startOffset,
      );
    }

    if (this.metrics.totalPixelsScrolled != undefined) {
      metrics['Total Pixels Scrolled (all directions)'] =
        this.metrics.totalPixelsScrolled;
    }

    return dictToTreeNodes(metrics);
  }

  private getDelayTable(): m.Child {
    if (this.orderedJankSlices.length > 0) {
      interface DelayData {
        jankLink: m.Child;
        dur: m.Child;
        delayedVSyncs: number;
      }

      const columns: ColumnDescriptor<DelayData>[] = [
        widgetColumn<DelayData>('Cause', (x) => x.jankLink),
        widgetColumn<DelayData>('Duration', (x) => x.dur),
        numberColumn<DelayData>('Delayed Vsyncs', (x) => x.delayedVSyncs),
      ];
      const data: DelayData[] = [];
      for (const jankSlice of this.orderedJankSlices) {
        data.push({
          jankLink: getSliceForTrack(
            jankSlice.jankSlice,
            ScrollJankV3TrackKind,
            jankSlice.cause,
          ),
          dur: m(DurationWidget, {dur: jankSlice.delayDur}),
          delayedVSyncs: jankSlice.delayVsync,
        });
      }

      const tableData = new TableData(data);

      return m(Table, {
        data: tableData,
        columns: columns,
      });
    } else {
      return sqlValueToString('None');
    }
  }

  private getDescriptionText(): m.Child {
    return m(
      MultiParagraphText,
      m(TextParagraph, {
        text: `The interval during which the user has started a scroll ending
                 after their finger leaves the screen and any resulting fling
                 animations have finished.`,
      }),
      m(TextParagraph, {
        text: `Note: This can contain periods of time where the finger is down
                 and not moving and no active scrolling is occurring.`,
      }),
      m(TextParagraph, {
        text: `Note: Sometimes if a user touches the screen quickly after 
                 letting go or Chrome was hung and got into a bad state. A new
                 scroll will start which will result in a slightly overlapping
                 scroll. This can occur due to the last scroll still outputting
                 frames (to get caught up) and the "new" scroll having started
                 producing frames after the user has started scrolling again.`,
      }),
    );
  }

  private getGraphText(): m.Child {
    return m(
      MultiParagraphText,
      m(TextParagraph, {
        text: `The scroll offset is the discrepancy in physical screen pixels
                 between two consecutive frames.`,
      }),
      m(TextParagraph, {
        text: `The overall curve of the graph indicates the direction (up or
                 down) by which the user scrolled over time.`,
      }),
      m(TextParagraph, {
        text: `Grey blocks in the graph represent intervals of jank
                 corresponding with the Chrome Scroll Janks track.`,
      }),
    );
  }

  viewTab() {
    if (this.isLoading() || this.data == undefined) {
      return m('h2', 'Loading');
    }

    const details = dictToTreeNodes({
      'Scroll ID': sqlValueToString(this.data.id),
      'Start time': m(Timestamp, {ts: this.data.ts}),
      'Duration': m(DurationWidget, {dur: this.data.dur}),
      'SQL ID': m(SqlRef, {table: 'chrome_scrolls', id: this.config.id}),
    });

    return m(
      DetailsShell,
      {
        title: this.getTitle(),
      },
      m(
        GridLayout,
        m(
          GridLayoutColumn,
          m(Section, {title: 'Details'}, m(Tree, details)),
          m(
            Section,
            {title: 'Slice Metrics'},
            m(Tree, this.renderMetricsDictionary()),
          ),
          m(
            Section,
            {title: 'Frame Presentation Delays'},
            this.getDelayTable(),
          ),
        ),
        m(
          GridLayoutColumn,
          m(Section, {title: 'Description'}, this.getDescriptionText()),
          m(
            Section,
            {title: 'Scroll Offsets Plot'},
            m(".div[style='padding-bottom:5px']", this.getGraphText()),
            this.scrollDeltas,
          ),
        ),
      ),
    );
  }

  getTitle(): string {
    return this.config.title;
  }

  isLoading() {
    return !this.loaded;
  }
}
