// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

module.exports = {
  'root': true,
  'env': {
    'browser': true,
    'es2020': true,
  },
  'parserOptions': {
    'ecmaVersion': 2020,
    'sourceType': 'module',
  },
  'rules': {
    // Enabled checks.
    'brace-style': ['error', '1tbs'],

    // https://google.github.io/styleguide/jsguide.html#features-arrays-trailing-comma
    // https://google.github.io/styleguide/jsguide.html#features-objects-use-trailing-comma
    'comma-dangle': ['error', 'always-multiline'],

    'curly': ['error', 'multi-line', 'consistent'],
    'new-parens': 'error',
    'no-array-constructor': 'error',
    'no-console': ['error', {allow: ['info', 'warn', 'error', 'assert']}],
    'no-debugger': 'error',
    'no-extra-boolean-cast': 'error',
    'no-extra-semi': 'error',
    'no-new-wrappers': 'error',
    'no-restricted-imports': ['error', {
      'paths': [{
        'name':  'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js',
        'importNames': ['Polymer'],
        'message': 'Use PolymerElement instead.',
      },
      {
        'name':  '//resources/polymer/v3_0/polymer/polymer_bundled.min.js',
        'importNames': ['Polymer'],
        'message': 'Use PolymerElement instead.',
      }],
    }],
    'no-restricted-properties': [
      'error',
      {
        'property': '__lookupGetter__',
        'message': 'Use Object.getOwnPropertyDescriptor',
      },
      {
        'property': '__lookupSetter__',
        'message': 'Use Object.getOwnPropertyDescriptor',
      },
      {
        'property': '__defineGetter__',
        'message': 'Use Object.defineProperty',
      },
      {
        'property': '__defineSetter__',
        'message': 'Use Object.defineProperty',
      },
      {
        'object': 'cr',
        'property': 'exportPath',
        'message': 'Use ES modules or cr.define() instead',
      },
    ],
    'no-restricted-syntax': ['error', {
      'selector': 'CallExpression[callee.object.name=JSON][callee.property.name=parse] > CallExpression[callee.object.name=JSON][callee.property.name=stringify]',
      'message': 'Don\'t use JSON.parse(JSON.stringify(...)) to clone objects. Use structuredClone() instead.',
    },
    {
      // https://google.github.io/styleguide/tsguide.html#return-type-only-generics
      'selector': 'TSAsExpression > CallExpression > MemberExpression[property.name=/^querySelector$/]',
      'message': 'Don\'t use \'querySelector(...) as Type\'. Use the type parameter, \'querySelector<Type>(...)\' instead',
    },
    {
      // https://google.github.io/styleguide/tsguide.html#return-type-only-generics
      'selector': 'TSAsExpression > TSNonNullExpression > CallExpression > MemberExpression[property.name=/^querySelector$/]',
      'message': 'Don\'t use \'querySelector(...)! as Type\'. Use the type parameter, \'querySelector<Type>(...)\', followed by an assertion instead',
    },
    {
      // https://google.github.io/styleguide/tsguide.html#return-type-only-generics
      'selector': 'TSAsExpression > CallExpression > MemberExpression[property.name=/^querySelectorAll$/]',
      'message': 'Don\'t use \'querySelectorAll(...) as Type\'. Use the type parameter, \'querySelectorAll<Type>(...)\' instead',
    },
    {
      // Prevent a common misuse of "!" operator.
      "selector": "TSNonNullExpression > CallExpression > MemberExpression[property.name=/^querySelectorAll$/]",
      "message": "Remove unnecessary \"!\" non-null operator after querySelectorAll(). It always returns a non-null result",
    },
    {
      // https://google.github.io/styleguide/jsguide.html#es-module-imports
      //  1) Matching only import URLs that have at least one '/' slash, to
      //     avoid false positives for NodeJS imports like
      //     `import fs from 'fs';`.
      //     Using '\u002F' instead of '/' as the suggested workaround for
      //     https://github.com/eslint/eslint/issues/16555
      //  2) Allowing extensions that have a length between 2-4 characters
      //     (for example js, css, json)
      "selector": "ImportDeclaration[source.value=/^.*\\u002F.*(?<!\\.[a-z]{2}|\\.[a-z]{3}|\\.[a-z]{4})$/]",
      "message": "Disallowed extensionless import. Explicitly specify the extension suffix."
    }],
    'no-throw-literal': 'error',
    'no-trailing-spaces': 'error',
    'no-var': 'error',
    'prefer-const': 'error',
    'quotes': ['error', 'single', {allowTemplateLiterals: true}],
    'semi': ['error', 'always'],

    // https://google.github.io/styleguide/jsguide.html#features-one-variable-per-declaration
    'one-var': ['error', {
      let: 'never',
      const: 'never',
    }],

    // TODO(dpapad): Add more checks according to our styleguide.
  },

  'overrides': [{
    'files': ['**/*.ts'],
    'parser': './third_party/node/node_modules/@typescript-eslint/parser/dist/index.js',
    'plugins': [
      '@typescript-eslint',
    ],
    'rules': {
      'no-unused-vars': 'off',
      '@typescript-eslint/no-unused-vars': [
        'error', {
          argsIgnorePattern: '^_',
          varsIgnorePattern: '^_',
        }
      ],

      // https://google.github.io/styleguide/tsguide.html#array-constructor
      // Note: The rule below only partially enforces the styleguide, since it
      // it does not flag invocations of the constructor with a single
      // parameter.
      'no-array-constructor': 'off',
      '@typescript-eslint/no-array-constructor': 'error',

      // https://google.github.io/styleguide/tsguide.html#automatic-semicolon-insertion
      'semi': 'off',
      '@typescript-eslint/semi': ['error'],

      // https://google.github.io/styleguide/tsguide.html#arrayt-type
      '@typescript-eslint/array-type': ['error', {
        default: 'array-simple',
      }],

      // https://google.github.io/styleguide/tsguide.html#type-assertions-syntax
      '@typescript-eslint/consistent-type-assertions': ['error', {
         assertionStyle: 'as',
      }],

      // https://google.github.io/styleguide/tsguide.html#interfaces-vs-type-aliases
      '@typescript-eslint/consistent-type-definitions': ['error', 'interface'],

      // https://google.github.io/styleguide/tsguide.html#import-type
      '@typescript-eslint/consistent-type-imports': 'error',

      // https://google.github.io/styleguide/tsguide.html#visibility
      '@typescript-eslint/explicit-member-accessibility': ['error', {
        accessibility: 'no-public',
        overrides: {
          parameterProperties: 'off',
        },
      }],

      // https://google.github.io/styleguide/jsguide.html#naming
      '@typescript-eslint/naming-convention': [
        'error',
        {
          selector: ['class', 'interface', 'typeAlias', 'enum', 'typeParameter'],
          format: ['StrictPascalCase'],
          filter: {
            regex: '^(' +
                // Exclude TypeScript defined interfaces HTMLElementTagNameMap
                // and HTMLElementEventMap.
                'HTMLElementTagNameMap|HTMLElementEventMap|' +
                // Exclude native DOM types which are always named like HTML<Foo>Element.
                'HTML[A-Za-z]{0,}Element|' +
                // Exclude native DOM interfaces.
                'UIEvent|UIEventInit|DOMError|' +
                // Exclude the deprecated WebUIListenerBehavior interface.
                'WebUIListenerBehavior)$',
            match: false,
          },
        },
        {
          selector: 'enumMember',
          format: ['UPPER_CASE'],
        },
        {
          selector: 'classMethod',
          format: ['strictCamelCase'],
          modifiers: ['public'],
        },
        {
          selector: 'classMethod',
          format: ['strictCamelCase'],
          modifiers: ['private'],
          trailingUnderscore: 'allow',

          // Disallow the 'Tap_' suffix, in favor of 'Click_' in event handlers.
          // Note: Unfortunately this ESLint rule does not provide a way to
          // customize the error message to better inform developers.
          custom: {
            regex: '^on[a-zA-Z0-9]+Tap$',
            match: false,
          },
        },
        {
          selector: 'classProperty',
          format: ['UPPER_CASE'],
          modifiers: ['private', 'static', 'readonly'],
        },
        {
          selector: 'classProperty',
          format: ['UPPER_CASE'],
          modifiers: ['public', 'static', 'readonly'],
        },
        {
          selector: 'classProperty',
          format: ['camelCase'],
          modifiers: ['public'],
        },
        {
          selector: 'classProperty',
          format: ['camelCase'],
          modifiers: ['private'],
          trailingUnderscore: 'allow',
        },
        {
          selector: 'parameter',
          format: ['camelCase'],
          leadingUnderscore: 'allow',
        },
        {
          selector: 'function',
          format: ['camelCase'],
        },
      ],

      // https://google.github.io/styleguide/tsguide.html#member-property-declarations
      '@typescript-eslint/member-delimiter-style': ['error', {
        multiline: {
          delimiter: 'comma',
          requireLast: true,
        },
        singleline: {
          delimiter: 'comma',
          requireLast: false,
        },
        overrides: {
          interface: {
            multiline: {
              delimiter: 'semi',
              requireLast: true,
            },
            singleline: {
              delimiter: 'semi',
              requireLast: false,
            },
          },
        },
      }],

      // https://google.github.io/styleguide/tsguide.html#wrapper-types
      '@typescript-eslint/ban-types': ['error', {
        extendDefaults: false,
        types: {
          String: {
            message: 'Use string instead',
            fixWith: 'string',
          },
          Boolean: {
            message: 'Use boolean instead',
            fixWith: 'boolean',
          },
          Number: {
            message: 'Use number instead',
            fixWith: 'number',
          },
          Symbol: {
            message: 'Use symbol instead',
            fixWith: 'symbol',
          },
          BigInt: {
            message: 'Use bigint instead',
            fixWith: 'bigint',
          },
        }
      }],
    }
  }]
};
