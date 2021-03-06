////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2016 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
////////////////////////////////////////////////////////////////////////////////

#include "CheckVersionFeature.h"

#include "Basics/exitcodes.h"
#include "Logger/Logger.h"
#include "Logger/LoggerFeature.h"
#include "ProgramOptions/ProgramOptions.h"
#include "ProgramOptions/Section.h"
#include "Replication/ReplicationFeature.h"
#include "RestServer/DatabaseFeature.h"
#include "VocBase/Methods/Version.h"
#include "VocBase/vocbase.h"

using namespace arangodb;
using namespace arangodb::application_features;
using namespace arangodb::basics;
using namespace arangodb::options;

CheckVersionFeature::CheckVersionFeature(
    ApplicationServer* server, int* result,
    std::vector<std::string> const& nonServerFeatures)
    : ApplicationFeature(server, "CheckVersion"),
      _checkVersion(false),
      _result(result),
      _nonServerFeatures(nonServerFeatures) {
  setOptional(false);
  requiresElevatedPrivileges(false);
  startsAfter("Database");
  startsAfter("EngineSelector");
  startsAfter("ServerId");
}

void CheckVersionFeature::collectOptions(
    std::shared_ptr<ProgramOptions> options) {
  options->addSection("database", "Configure the database");

  options->addOldOption("check-version", "database.check-version");

  options->addHiddenOption("--database.check-version",
                           "checks the versions of the database and exit",
                           new BooleanParameter(&_checkVersion));
}

void CheckVersionFeature::validateOptions(
    std::shared_ptr<ProgramOptions> options) {
  if (!_checkVersion) {
    return;
  }

  ApplicationServer::forceDisableFeatures(_nonServerFeatures);

  LoggerFeature* logger =
      ApplicationServer::getFeature<LoggerFeature>("Logger");
  logger->disableThreaded();

  ReplicationFeature* replicationFeature =
      ApplicationServer::getFeature<ReplicationFeature>("Replication");
  replicationFeature->disableReplicationApplier();

  DatabaseFeature* databaseFeature =
      ApplicationServer::getFeature<DatabaseFeature>("Database");
  databaseFeature->enableCheckVersion();
}

void CheckVersionFeature::start() {
  if (!_checkVersion) {
    return;
  }

  // check the version
  if (DatabaseFeature::DATABASE->isInitiallyEmpty()) {
    *_result = EXIT_SUCCESS;
  } else {
    checkVersion();
  }

  // and force shutdown
  server()->beginShutdown();

  std::this_thread::sleep_for(std::chrono::microseconds(1 * 1000 * 1000));
  TRI_EXIT_FUNCTION(EXIT_SUCCESS, nullptr);
}

void CheckVersionFeature::checkVersion() {
  *_result = 1;

  // run version check
  LOG_TOPIC(TRACE, arangodb::Logger::FIXME) << "starting version check";
  // can do this without a lock as this is the startup
  DatabaseFeature* databaseFeature =
      application_features::ApplicationServer::getFeature<DatabaseFeature>(
          "Database");

  // iterate over all databases
  for (auto& name : databaseFeature->getDatabaseNames()) {
    TRI_vocbase_t* vocbase = databaseFeature->lookupDatabase(name);
    methods::VersionResult res = methods::Version::check(vocbase);
    TRI_ASSERT(vocbase != nullptr);

    LOG_TOPIC(DEBUG, Logger::STARTUP) << "version check return status "
                                      << res.status;
    if (res.status < 0) {
      LOG_TOPIC(FATAL, arangodb::Logger::FIXME)
          << "Database version check failed for '" << vocbase->name()
          << "'. Please inspect the logs for any errors";
      FATAL_ERROR_EXIT_CODE(TRI_EXIT_VERSION_CHECK_FAILED);
    } else if (res.status == methods::VersionResult::DOWNGRADE_NEEDED) {
      // this is safe to do even if further databases will be checked
      // because we will never set the status back to success
      *_result = 3;
      LOG_TOPIC(WARN, arangodb::Logger::FIXME)
          << "Database version check failed for '" << vocbase->name()
          << "': upgrade needed";
    } else if (res.status == methods::VersionResult::UPGRADE_NEEDED &&
               *_result == 1) {
      // this is safe to do even if further databases will be checked
      // because we will never set the status back to success
      *_result = 2;
      LOG_TOPIC(WARN, arangodb::Logger::FIXME)
          << "Database version check failed for '" << vocbase->name()
          << "': downgrade needed";
    }
  }

  LOG_TOPIC(DEBUG, Logger::STARTUP) << "final result of version check: "
                                    << *_result;

  if (*_result == 1) {
    *_result = EXIT_SUCCESS;
  } else if (*_result > 1) {
    if (*_result == 2) {
      // downgrade needed
      LOG_TOPIC(FATAL, Logger::FIXME)
          << "Database version check failed: downgrade needed";
      FATAL_ERROR_EXIT_CODE(TRI_EXIT_DOWNGRADE_REQUIRED);
    } else if (*_result == 3) {
      LOG_TOPIC(FATAL, Logger::FIXME)
          << "Database version check failed: upgrade needed";
      FATAL_ERROR_EXIT_CODE(TRI_EXIT_UPGRADE_REQUIRED);
    } else {
      LOG_TOPIC(FATAL, Logger::FIXME) << "Database version check failed";
      FATAL_ERROR_EXIT_CODE(TRI_EXIT_VERSION_CHECK_FAILED);
    }
    FATAL_ERROR_EXIT_CODE(*_result);
  }
}
