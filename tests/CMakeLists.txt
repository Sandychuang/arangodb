foreach (LINK_DIR ${V8_LINK_DIRECTORIES})
  link_directories("${LINK_DIR}")
endforeach()

################################################################################
## IResearch
################################################################################

if (USE_IRESEARCH)
  set(IRESEARCH_TESTS_SOURCES
    IResearch/common.cpp
#    IResearch/AttributeScorer-test.cpp
    IResearch/Containers-test.cpp
    IResearch/IResearchAnalyzerFeature-test.cpp
    IResearch/IResearchFeature-test.cpp
    IResearch/IResearchOrder-test.cpp
    IResearch/IResearchView-test.cpp
    IResearch/IResearchViewMeta-test.cpp
    IResearch/IResearchDocument-test.cpp
    IResearch/IResearchFilter-test.cpp
    IResearch/IResearchFilterFunction-test.cpp
    IResearch/IResearchFilterBoolean-test.cpp
    IResearch/IResearchFilterCompare-test.cpp
    IResearch/IResearchFilterIn-test.cpp
    IResearch/IResearchIndex-test.cpp
    IResearch/IResearchLink-test.cpp
    IResearch/IResearchLinkMeta-test.cpp
    IResearch/IResearchQuery-test.cpp
    IResearch/IResearchQueryAnd-test.cpp
    IResearch/IResearchQueryBooleanTerm-test.cpp
    IResearch/IResearchQueryComplexBoolean-test.cpp
    IResearch/IResearchQueryExists-test.cpp
    IResearch/IResearchQueryIn-test.cpp
    IResearch/IResearchQueryNullTerm-test.cpp
    IResearch/IResearchQueryNumericTerm-test.cpp
    IResearch/IResearchQueryOr-test.cpp
    IResearch/IResearchQueryPhrase-test.cpp
    IResearch/IResearchQuerySelectAll-test.cpp
    IResearch/IResearchQueryStartsWith-test.cpp
    IResearch/IResearchQueryStringTerm-test.cpp
    IResearch/IResearchQueryTokens-test.cpp
    IResearch/IResearchQueryValue-test.cpp
    IResearch/IResearchQueryJoin-test.cpp
    IResearch/IResearchQueryAggregate-test.cpp
    IResearch/IResearchQueryTraversal-test.cpp
    IResearch/ExpressionFilter-test.cpp
    IResearch/ExecutionBlockMock.cpp
    IResearch/ExpressionContextMock.cpp
    IResearch/VelocyPackHelper-test.cpp
    IResearch/ExecutionBlockMock-test.cpp
    Utils/CollectionNameResolver-test.cpp
    VocBase/LogicalDataSource-test.cpp
    VocBase/vocbase-test.cpp
  )
endif ()

add_executable(
  arangodbtests
  ${CMAKE_SOURCE_DIR}/arangod/RestServer/FlushFeature.cpp
  Agency/AddFollowerTest.cpp
  Agency/CleanOutServerTest.cpp
  Agency/FailedFollowerTest.cpp
  Agency/FailedLeaderTest.cpp
  Agency/FailedServerTest.cpp
  Agency/MoveShardTest.cpp
  Agency/RemoveFollowerTest.cpp
  Agency/StoreTest.cpp
  Agency/SupervisionTest.cpp
  Aql/EngineInfoContainerCoordinatorTest.cpp
  Aql/RestAqlHandlerTest.cpp
  Auth/UserManagerTest.cpp
  Basics/icu-helper.cpp
  Basics/ApplicationServerTest.cpp
  Basics/AttributeNameParserTest.cpp
  Basics/associative-multi-pointer-test.cpp
  Basics/associative-multi-pointer-nohashcache-test.cpp
  Basics/conversions-test.cpp
  Basics/csv-test.cpp
  Basics/files-test.cpp
  Basics/fpconv-test.cpp
  Basics/json-test.cpp
  Basics/hashes-test.cpp
  Basics/skiplist-test.cpp
  Basics/priorityqueue-test.cpp
  Basics/string-buffer-test.cpp
  Basics/string-utf8-normalize-test.cpp
  Basics/string-utf8-test.cpp
  Basics/string-test.cpp
  Basics/vector-test.cpp
  Basics/structure-size-test.cpp
  Basics/EndpointTest.cpp
  Basics/LoggerTest.cpp
  Basics/StringBufferTest.cpp
  Basics/StringUtilsTest.cpp
  Basics/VelocyPackHelper-test.cpp
  Cache/BucketState.cpp
  Cache/CachedValue.cpp
  Cache/FrequencyBuffer.cpp
  Cache/Manager.cpp
  Cache/Metadata.cpp
  Cache/MockScheduler.cpp
  Cache/PlainBucket.cpp
  Cache/PlainCache.cpp
  Cache/Rebalancer.cpp
  Cache/Table.cpp
  Cache/TransactionalBucket.cpp
  Cache/TransactionalCache.cpp
  Cache/TransactionalStore.cpp
  Cache/TransactionManager.cpp
  Cache/TransactionsWithBackingStore.cpp
  Cluster/ClusterCommTest.cpp
  Cluster/ClusterHelpersTest.cpp
  Cluster/ShardDistributionReporterTest.cpp
  Geo/georeg.cpp
  Graph/ClusterTraverserCacheTest.cpp
  Pregel/typedbuffer.cpp
  RocksDBEngine/KeyTest.cpp
  RocksDBEngine/IndexEstimatorTest.cpp
  RocksDBEngine/TypeConversionTest.cpp
  SimpleHttpClient/CommunicatorTest.cpp
  IResearch/StorageEngineMock.cpp
  ${IRESEARCH_TESTS_SOURCES}
  main.cpp
)

target_link_libraries(
  arangodbtests
  arangoserver
)

target_include_directories(arangodbtests PRIVATE
  ${INCLUDE_DIRECTORIES}
)

# add these includes as system includes because otherwise
# the compiler will emit warnings for fakeit.hpp
target_include_directories(arangodbtests SYSTEM PRIVATE
  ${CMAKE_SOURCE_DIR}/3rdParty/catch
  ${CMAKE_SOURCE_DIR}/3rdParty/fakeit
)

if (USE_IRESEARCH)
  find_package(OpenSSL REQUIRED)
  list(APPEND IRESEARCH_LIB_RESOURCES
    "${LIB_EAY_DEBUG_DLL}"
    "${SSL_EAY_DEBUG_DLL}"
    "${LIB_EAY_RELEASE_DLL}"
    "${SSL_EAY_RELEASE_DLL}"
  )

  foreach(ELEMENT ${IRESEARCH_LIB_RESOURCES})
    add_custom_command(
      TARGET arangodbtests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E echo "copying library resource:" "${ELEMENT}" " -> " "$<TARGET_FILE_DIR:arangodbtests>"
      COMMAND cp -lf ${ELEMENT} $<TARGET_FILE_DIR:arangodbtests> || ${CMAKE_COMMAND} -E copy ${ELEMENT} $<TARGET_FILE_DIR:arangodbtests>
    )
  endforeach()
endif()
