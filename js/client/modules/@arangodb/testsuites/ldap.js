/* jshint strict: false, sub: true */
/* global print */
'use strict';

// //////////////////////////////////////////////////////////////////////////////
// / DISCLAIMER
// /
// / Copyright 2016 ArangoDB GmbH, Cologne, Germany
// / Copyright 2014 triagens GmbH, Cologne, Germany
// /
// / Licensed under the Apache License, Version 2.0 (the "License")
// / you may not use this file except in compliance with the License.
// / You may obtain a copy of the License at
// /
// /     http://www.apache.org/licenses/LICENSE-2.0
// /
// / Unless required by applicable law or agreed to in writing, software
// / distributed under the License is distributed on an "AS IS" BASIS,
// / WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// / See the License for the specific language governing permissions and
// / limitations under the License.
// /
// / Copyright holder is ArangoDB GmbH, Cologne, Germany
// /
// / @author Heiko Kernbach
// //////////////////////////////////////////////////////////////////////////////

const functionsDocumentation = {
  'ldap': 'ldap tests'
};
const optionsDocumentation = [
  '   - `skipLdap` : if set to true the LDAP tests are skipped',
  '   - `ldapHost : Host/IP of the ldap server',
  '   - `ldapPort : Port of the ldap server'
];

// const helper = require('@arangodb/user-helper');
const _ = require('lodash');
// const fs = require('fs');
// const pu = require('@arangodb/process-utils');
const tu = require('@arangodb/test-utils');
// const request = require('@arangodb/request');

// const BLUE = require('internal').COLORS.COLOR_BLUE;
const CYAN = require('internal').COLORS.COLOR_CYAN;
// const GREEN = require('internal').COLORS.COLOR_GREEN;
// const RED = require('internal').COLORS.COLOR_RED;
const RESET = require('internal').COLORS.COLOR_RESET;
// const YELLOW = require('internal').COLORS.COLOR_YELLOW;

// //////////////////////////////////////////////////////////////////////////////
// / @brief TEST: ldap
// //////////////////////////////////////////////////////////////////////////////

const tests = {
  ldapModeRoles: {
    name: 'ldapModeRoles',
    conf: {
      'server.authentication': true,
      'server.authentication-system-only': true,
      'server.jwt-secret': 'haxxmann', // hardcoded in auth.js
      'ldap.enabled': true,
      'ldap.server': '127.0.0.1',
      'ldap.port': '389',
      'ldap.binddn': 'cn=admin,dc=arangodb,dc=com',
      'ldap.bindpasswd': 'password',
      'ldap.basedn': 'dc=arangodb,dc=com',
      'ldap.superuser-role': 'adminrole',
      // Use Roles Attribute Mode #1
      'ldap.roles-attribute-name': 'sn',
      // 'log.level': 'ldap=trace',
      'javascript.allow-admin-execute': 'true',
      'server.local-authentication': 'true'
    }
  },
  ldapModeSearch: {
    name: 'ldapModeSearch',
    conf: {
      'server.authentication': true,
      'server.authentication-system-only': true,
      'server.jwt-secret': 'haxxmann', // hardcoded in auth.js
      'ldap.enabled': true,
      'ldap.port': '389',
      'ldap.basedn': 'dc=arangodb,dc=com',
      'ldap.server': '127.0.0.1',
      'ldap.binddn': 'cn=admin,dc=arangodb,dc=com',
      'ldap.bindpasswd': 'password',
      // Search Mode activate the following for RoleSearch:
      'ldap.search-filter': 'objectClass=*',
      'ldap.search-attribute': 'uid',
      'ldap.roles-search': '(&(objectClass=groupOfUniqueNames)(uniqueMember={USER}))',
      // 'log.level': 'ldap=trace',
      'javascript.allow-admin-execute': 'true',
      'ldap.superuser-role': 'adminrole',
      'server.local-authentication': 'true'
    }
  },
  ldapModeRolesPrefixSuffix: {
    name: 'ldapModeRolesPrefixSuffix',
    conf: {
      'server.authentication': true,
      'server.authentication-system-only': true,
      'server.jwt-secret': 'haxxmann', // hardcoded in auth.js
      'ldap.enabled': true,
      'ldap.server': '127.0.0.1',
      'ldap.port': '389',
      'ldap.binddn': 'cn=admin,dc=arangodb,dc=com',
      'ldap.bindpasswd': 'password',
      'ldap.basedn': 'dc=arangodb,dc=com',
      'ldap.superuser-role': 'adminrole',
      // Use Roles Attribute Mode #3
      'ldap.roles-attribute-name': 'sn',
      // Use Simple Login Mode
      'ldap.prefix': 'uid=',
      'ldap.suffix': ',dc=arangodb,dc=com',
      // 'log.level': 'ldap=trace',
      'javascript.allow-admin-execute': 'true',
      'server.local-authentication': 'true'
    }
  },
  ldapModeSearchPrefixSuffix: {
    name: 'ldapModeSearchPrefixSuffix',
    conf: {
      'server.authentication': true,
      'server.authentication-system-only': true,
      'server.jwt-secret': 'haxxmann', // hardcoded in auth.js
      'ldap.enabled': true,
      'ldap.port': '389',
      'ldap.basedn': 'dc=arangodb,dc=com',
      'ldap.server': '127.0.0.1',
      'ldap.binddn': 'cn=admin,dc=arangodb,dc=com',
      'ldap.bindpasswd': 'password',
      // Search Mode activate the following for RoleSearch:
      'ldap.search-filter': 'objectClass=*',
      'ldap.search-attribute': 'uid',
      'ldap.roles-search': '(&(objectClass=groupOfUniqueNames)(uniqueMember={USER}))',
      // Use Simple Login Mode
      'ldap.prefix': 'uid=',
      'ldap.suffix': ',dc=arangodb,dc=com',
      // 'log.level': 'ldap=trace',
      'javascript.allow-admin-execute': 'true',
      'ldap.superuser-role': 'adminrole',
      'server.local-authentication': 'true'
    }
  }
};

function parseOptions (options) {
  let toReturn = tests;

  _.each(toReturn, function (opt) {
    if (options.ldapHost) {
      opt.ldapHost = options.ldapHost;
    }
    if (options.ldapPort) {
      opt.ldapPort = options.ldapPort;
    }
  });
  return toReturn;
}

function authenticationLdapAllModes (options) {
  if (options.skipLdap === true) {
    print('skipping Ldap Authentication tests!');
    return {
      authenticationLdapPermissions: {
        status: true,
        skipped: true
      }
    };
  }
  const opts = parseOptions(options);
  if (options.cluster) {
    options.dbServers = 2;
    options.coordinators = 2;
  }

  print(CYAN + 'Running all client LDAP Permission tests...' + RESET);
  // let testCases = tu.scanTestPath('js/client/tests/ldap');
  let testCases = tu.scanTestPath('js/client/tests/authentication');

  // #1 role mode
  print('Performing #1 Test: Role Mode');
  print(opts.ldapModeRoles.conf);
  tu.performTests(options, testCases, 'ldap', tu.runInArangosh, opts.ldapModeRoles.conf);
  // #2 search mode
  print('Performing #2 Test: Search Mode');
  print(opts.ldapModeSearch.conf);
  tu.performTests(options, testCases, 'ldap', tu.runInArangosh, opts.ldapModeSearch.conf);
  // #3 role mode with prefix and suffix
  print('Performing #3 Test: Role Mode - Simple Login Mode');
  print(opts.ldapModeRolesPrefixSuffix.conf);
  tu.performTests(options, testCases, 'ldap', tu.runInArangosh, opts.ldapModeRolesPrefixSuffix.conf);
  // #3 search mode with prefix and suffix
  print('Performing #4 Test: Search Mode - Simple Login Mode');
  print(opts.ldapModeSearchPrefixSuffix.conf);
  tu.performTests(options, testCases, 'ldap', tu.runInArangosh, opts.ldapModeSearchPrefixSuffix.conf);
}

function authenticationLdapSearchModePrefixSuffix (options) {
  if (options.skipLdap === true) {
    print('skipping Ldap Authentication tests!');
    return {
      authenticationLdapPermissions: {
        status: true,
        skipped: true
      }
    };
  }
  const opts = parseOptions(options);
  if (options.cluster) {
    options.dbServers = 2;
    options.coordinators = 2;
  }

  print(CYAN + 'Client LDAP Search Mode Permission tests...' + RESET);
  // let testCases = tu.scanTestPath('js/client/tests/ldap');
  let testCases = tu.scanTestPath('js/client/tests/authentication');

  print('Performing #4 Test: Search Mode - Simple Login Mode');
  print(opts.ldapModeSearchPrefixSuffix.conf);
  return tu.performTests(options, testCases, 'ldap', tu.runInArangosh, opts.ldapModeSearchPrefixSuffix.conf);
}

function authenticationLdapSearchMode (options) {
  if (options.skipLdap === true) {
    print('skipping Ldap Authentication tests!');
    return {
      authenticationLdapPermissions: {
        status: true,
        skipped: true
      }
    };
  }
  const opts = parseOptions(options);
  if (options.cluster) {
    options.dbServers = 2;
    options.coordinators = 2;
  }

  print(CYAN + 'Client LDAP Search Mode Permission tests...' + RESET);
  // let testCases = tu.scanTestPath('js/client/tests/ldap');
  let testCases = tu.scanTestPath('js/client/tests/authentication');

  print('Performing #2 Test: Search Mode');
  print(opts.ldapModeSearch.conf);
  return tu.performTests(options, testCases, 'ldap', tu.runInArangosh, opts.ldapModeSearch.conf);
}

function authenticationLdapRolesModePrefixSuffix (options) {
  if (options.skipLdap === true) {
    print('skipping Ldap Authentication tests!');
    return {
      authenticationLdapPermissions: {
        status: true,
        skipped: true
      }
    };
  }
  const opts = parseOptions(options);
  if (options.cluster) {
    options.dbServers = 2;
    options.coordinators = 2;
  }

  print(CYAN + 'Client LDAP Permission tests...' + RESET);
  // let testCases = tu.scanTestPath('js/client/tests/ldap');
  let testCases = tu.scanTestPath('js/client/tests/authentication');

  print('Performing #3 Test: Role Mode - Simple Login Mode');
  print(opts.ldapModeRolesPrefixSuffix.conf);
  return tu.performTests(options, testCases, 'ldap', tu.runInArangosh, opts.ldapModeRolesPrefixSuffix.conf);
}

function authenticationLdapRolesMode (options) {
  if (options.skipLdap === true) {
    print('skipping Ldap Authentication tests!');
    return {
      authenticationLdapPermissions: {
        status: true,
        skipped: true
      }
    };
  }
  const opts = parseOptions(options);
  if (options.cluster) {
    options.dbServers = 2;
    options.coordinators = 2;
  }

  print(CYAN + 'Client LDAP Permission tests...' + RESET);
  // let testCases = tu.scanTestPath('js/client/tests/ldap');
  let testCases = tu.scanTestPath('js/client/tests/authentication');

  print('Performing #1 Test: Role Mode');
  print(opts.ldapModeRoles.conf);
  return tu.performTests(options, testCases, 'ldap', tu.runInArangosh, opts.ldapModeRoles.conf);
}

function setup (testFns, defaultFns, opts, fnDocs, optionsDoc) {
  // testFns['ldap_start'] = startLdap;
  testFns['ldap'] = authenticationLdapAllModes;
  testFns['ldaprole'] = authenticationLdapRolesMode;
  testFns['ldapsearch'] = authenticationLdapSearchMode;
  testFns['ldaprolesimple'] = authenticationLdapRolesModePrefixSuffix;
  testFns['ldapsearchsimple'] = authenticationLdapSearchModePrefixSuffix;

  // defaultFns.push('ldap'); // turn off ldap tests by default
  // turn off ldap tests by default.
  opts['skipLdap'] = true;

  // only enable them in enterprise version
  let version = {};
  if (global.ARANGODB_CLIENT_VERSION) {
    version = global.ARANGODB_CLIENT_VERSION(true);
    if (version['enterprise-version']) {
      opts['skipLdap'] = false;
    }
  }

  for (var attrname in functionsDocumentation) { fnDocs[attrname] = functionsDocumentation[attrname]; }
  for (var i = 0; i < optionsDocumentation.length; i++) { optionsDoc.push(optionsDocumentation[i]); }
}

exports.setup = setup;
