////////////////////////////////////////////////////////////////////////////////
/// @brief V8 Traverser
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2014-2015 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
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
/// @author Michael Hackstein
/// @author Copyright 2014-2015, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2012-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "v8.h"
#include "V8/v8-conv.h"
#include "V8/v8-utils.h"
#include "V8Server/v8-vocbaseprivate.h"
#include "V8Server/v8-wrapshapedjson.h"
#include "V8Server/v8-vocindex.h"
#include "V8Server/v8-collection.h"
#include "Utils/transactions.h"
#include "Utils/V8ResolverGuard.h"
#include "Utils/CollectionNameResolver.h"
#include "VocBase/document-collection.h"
#include "Traverser.h"
#include "VocBase/key-generator.h"

using namespace std;
using namespace triagens::basics;
using namespace triagens::arango;

class SimpleEdgeExpander {

  private:
////////////////////////////////////////////////////////////////////////////////
/// @brief direction of expansion
////////////////////////////////////////////////////////////////////////////////

    TRI_edge_direction_e direction;

////////////////////////////////////////////////////////////////////////////////
/// @brief edge collection
////////////////////////////////////////////////////////////////////////////////

    TRI_document_collection_t* edgeCollection;

////////////////////////////////////////////////////////////////////////////////
/// @brief collection name and /
////////////////////////////////////////////////////////////////////////////////

    string edgeIdPrefix;

////////////////////////////////////////////////////////////////////////////////
/// @brief the collection name resolver
////////////////////////////////////////////////////////////////////////////////

    CollectionNameResolver* resolver;

////////////////////////////////////////////////////////////////////////////////
/// @brief this indicates whether or not a distance attribute is used or
/// whether all edges are considered to have weight 1
////////////////////////////////////////////////////////////////////////////////

    bool usesDist;

////////////////////////////////////////////////////////////////////////////////
/// @brief an ID for the expander, just for debuggin
////////////////////////////////////////////////////////////////////////////////

    string id;

  public: 

    SimpleEdgeExpander(TRI_edge_direction_e direction,
                       TRI_document_collection_t* edgeCollection,
                       string edgeCollectionName,
                       CollectionNameResolver* resolver,
                       string id) 
      : direction(direction), edgeCollection(edgeCollection),
        resolver(resolver), usesDist(false), id(id)
    {
      edgeIdPrefix = edgeCollectionName + "/";
    };

    Traverser::VertexId extractFromId(TRI_doc_mptr_copy_t& ptr) {
      char const* key = TRI_EXTRACT_MARKER_FROM_KEY(&ptr);
      TRI_voc_cid_t cid = TRI_EXTRACT_MARKER_FROM_CID(&ptr);
      string col = resolver->getCollectionName(cid);
      col.append("/");
      col.append(key);
      return col;
    };

    Traverser::VertexId extractToId(TRI_doc_mptr_copy_t& ptr) {
      char const* key = TRI_EXTRACT_MARKER_TO_KEY(&ptr);
      TRI_voc_cid_t cid = TRI_EXTRACT_MARKER_TO_CID(&ptr);
      string col = resolver->getCollectionName(cid);
      col.append("/");
      col.append(key);
      return col;
    };

    Traverser::EdgeId extractEdgeId(TRI_doc_mptr_copy_t ptr) {
      char const* key = TRI_EXTRACT_MARKER_KEY(ptr);
      // AppendAndCopy
      return edgeIdPrefix.append(key);
    };

    void operator() (Traverser::VertexId source,
                     vector<Traverser::Step>& result) {
      std::cout << "Expander " << id << " called." << std::endl;
      std::vector<TRI_doc_mptr_copy_t> edges;
      TransactionBase fake(true); // Fake a transaction to please checks. 
                                  // This is due to multi-threading

      // Process Vertex Id!
      size_t split;
      char const* str = source.c_str();
      if (!TRI_ValidateDocumentIdKeyGenerator(str, &split)) {
        // TODO Error Handling
        return;
      }
      string collectionName = source.substr(0, split);

      auto col = resolver->getCollectionStruct(collectionName);
      if (col == nullptr) {
        // collection not found
        throw TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
      }
      auto collectionCId = col->_cid;
      edges = TRI_LookupEdgesDocumentCollection(edgeCollection, direction, collectionCId, const_cast<char*>(str + split + 1));
      std::cout << "Expander " << id << " got edges." << std::endl;
        
      unordered_map<Traverser::VertexId, size_t> candidates;
      Traverser::VertexId from;
      Traverser::VertexId to;
      if (usesDist) {
        for (size_t j = 0;  j < edges.size(); ++j) {
          from = extractFromId(edges[j]);
          to = extractToId(edges[j]);
          if (from != source) {
            auto cand = candidates.find(from);
            if (cand == candidates.end()) {
              // Add weight
              result.emplace_back(to, from, 1, extractEdgeId(edges[j]));
              candidates.emplace(from, result.size()-1);
            } else {
              // Compare weight
              // use result[cand->seconde].weight() and .setWeight()
            }
          } 
          else if (to != source) {
            auto cand = candidates.find(to);
            if (cand == candidates.end()) {
              // Add weight
              result.emplace_back(to, from, 1, extractEdgeId(edges[j]));
              candidates.emplace(to, result.size()-1);
            } else {
              // Compare weight
              // use result[cand->seconde].weight() and .setWeight()
            }
          }
        }
      } 
      else {
        for (size_t j = 0;  j < edges.size();  ++j) {
          from = extractFromId(edges[j]);
          to = extractFromId(edges[j]);
          if (from != source) {
            auto cand = candidates.find(from);
            if (cand == candidates.end()) {
              result.emplace_back(from, to, 1, extractEdgeId(edges[j]));
              candidates.emplace(from, result.size()-1);
            }
          } 
          else if (to != source) {
            auto cand = candidates.find(to);
            if (cand == candidates.end()) {
              result.emplace_back(to, from, 1, extractEdgeId(edges[j]));
              candidates.emplace(to, result.size()-1);
            }
          }
        }
      }
      std::cout << "Expander " << id << " done." << std::endl;
    }
};

static v8::Handle<v8::Value> pathIdsToV8(v8::Isolate* isolate, Traverser::Path& p) {
  v8::EscapableHandleScope scope(isolate);
  TRI_GET_GLOBALS();
  v8::Handle<v8::Object> result = v8::Object::New(isolate);

  uint32_t const vn = static_cast<uint32_t>(p.vertices.size());
  v8::Handle<v8::Array> vertices = v8::Array::New(isolate, static_cast<int>(vn));

  for (size_t j = 0;  j < vn;  ++j) {
    vertices->Set(static_cast<uint32_t>(j), TRI_V8_ASCII_STRING(p.vertices[j]));
  }
  result->Set("vertices", vertices);

  uint32_t const en = static_cast<uint32_t>(p.edges.size());
  v8::Handle<v8::Array> edges = v8::Array::New(isolate, static_cast<int>(en));

  for (size_t j = 0;  j < en;  ++j) {
    edges->Set(static_cast<uint32_t>(j), TRI_V8_ASCII_STRING(p.edges[j]));
  }
  result->Set(TRI_V8_STRING("edges"), edges);
  result->Set(TRI_V8_STRING("distance"), v8::Number::New(isolate, p.weight));

  return result;
};

struct LocalCollectionGuard {
  LocalCollectionGuard (TRI_vocbase_col_t* collection)
    : _collection(collection) {
  }

  ~LocalCollectionGuard () {
    if (_collection != nullptr && ! _collection->_isLocal) {
      FreeCoordinatorCollection(_collection);
    }
  }

  TRI_vocbase_col_t* _collection;
};

void TRI_RunDijkstraSearch (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);

  if (args.Length() < 4 || args.Length() > 5) {
    TRI_V8_THROW_EXCEPTION_USAGE("AQL_SHORTEST_PATH(<vertexcollection>, <edgecollection>, <start>, <end>, <options>)");
  }

  std::unique_ptr<char[]> key;
  TRI_vocbase_t* vocbase;
  TRI_vocbase_col_t const* col = nullptr;

  vocbase = GetContextVocBase(isolate);

  vector<string> readCollections;
  vector<string> writeCollections;
  TRI_voc_cid_t vertexCollectionCId;
  TRI_voc_cid_t edgeCollectionCId;
  TRI_voc_rid_t rid;

  double lockTimeout = (double) (TRI_TRANSACTION_DEFAULT_LOCK_TIMEOUT / 1000000ULL);
  bool embed = false;
  bool waitForSync = false;

  // get the vertex collection
  if (! args[0]->IsString()) {
    TRI_V8_THROW_TYPE_ERROR("expecting string for <vertexcollection>");
  }
  string vertexCollectionName = TRI_ObjectToString(args[0]);

  // get the edge collection
  if (! args[1]->IsString()) {
    TRI_V8_THROW_TYPE_ERROR("expecting string for <edgecollection>");
  }
  string const edgeCollectionName = TRI_ObjectToString(args[1]);

  vocbase = GetContextVocBase(isolate);

  if (vocbase == nullptr) {
    TRI_V8_THROW_EXCEPTION(TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }
  V8ResolverGuard resolver(vocbase);

  readCollections.push_back(vertexCollectionName);
  readCollections.push_back(edgeCollectionName);
  
  if (! args[2]->IsString()) {
    TRI_V8_THROW_TYPE_ERROR("expecting string for <startVertex>");
  }


  std::string vertexColName;

  if (! ExtractDocumentHandle(isolate, args[2], vertexColName, key, rid)) {
    TRI_V8_THROW_EXCEPTION(TRI_ERROR_ARANGO_DOCUMENT_HANDLE_BAD);
  }

  if (key.get() == nullptr) {
    TRI_V8_THROW_EXCEPTION(TRI_ERROR_ARANGO_DOCUMENT_HANDLE_BAD);
  }

  TRI_ASSERT(key.get() != nullptr);

  // IHHF isCoordinator

  // Start Transaction to collect all parts of the path
  ExplicitTransaction trx(
    vocbase,
    readCollections,
    writeCollections,
    lockTimeout,
    waitForSync,
    embed
  );
  
  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_THROW_EXCEPTION(res);
  }

  col = resolver.getResolver()->getCollectionStruct(vertexCollectionName);
  if (col == nullptr) {
    // collection not found
    TRI_V8_THROW_EXCEPTION(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
  }
  vertexCollectionCId = col->_cid;
  if (trx.orderBarrier(trx.trxCollection(col->_cid)) == nullptr) {
    TRI_V8_THROW_EXCEPTION_MEMORY();
  }

  col = resolver.getResolver()->getCollectionStruct(edgeCollectionName);
  if (col == nullptr) {
    // collection not found
    TRI_V8_THROW_EXCEPTION(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
  }
  edgeCollectionCId = col->_cid;
  if (trx.orderBarrier(trx.trxCollection(col->_cid)) == nullptr) {
    TRI_V8_THROW_EXCEPTION_MEMORY();
  }

  TRI_document_collection_t* ecol = trx.trxCollection(col->_cid)->_collection->_collection;
  CollectionNameResolver resolver1(vocbase);
  CollectionNameResolver resolver2(vocbase);
  SimpleEdgeExpander forwardExpander(TRI_EDGE_OUT, ecol, edgeCollectionName, &resolver1, "A");
  SimpleEdgeExpander backwardExpander(TRI_EDGE_IN, ecol, edgeCollectionName, &resolver2, "B");

  Traverser traverser(forwardExpander, backwardExpander);
  unique_ptr<Traverser::Path> path(traverser.shortestPath(startVertex, targetVertex));
  if (path.get() == nullptr) {
    res = trx.finish(res);
    v8::EscapableHandleScope scope(isolate);
    TRI_V8_RETURN(scope.Escape<v8::Value>(v8::Object::New(isolate)));
  }
  auto result = pathIdsToV8(isolate, *path);
  res = trx.finish(res);

  // This is how to get the data out of the collections!
  // Vertices
  TRI_doc_mptr_copy_t document;
  res = trx.readSingle(trx.trxCollection(vertexCollectionCId), &document, key.get());

  // Edges TRI_EDGE_OUT is hardcoded
  TRI_document_collection_t* ecol = trx.trxCollection(edgeCollectionCId)->_collection->_collection;
  std::vector<TRI_doc_mptr_copy_t>&& edges = TRI_LookupEdgesDocumentCollection(ecol, TRI_EDGE_OUT, vertexCollectionCId, key.get());

  // Add Dijkstra here

  // Now build up the result use Subtransactions for each used collection
  if (res == TRI_ERROR_NO_ERROR) {
    {
      // Collect all vertices
      SingleCollectionReadOnlyTransaction subtrx(new V8TransactionContext(true), vocbase, vertexCollectionCId);

      res = subtrx.begin();

      if (res != TRI_ERROR_NO_ERROR) {
        TRI_V8_THROW_EXCEPTION(res);
      }

      result = TRI_WrapShapedJson(isolate, subtrx, vertexCollectionCId, document.getDataPtr());

      if (document.getDataPtr() == nullptr) {
        res = TRI_ERROR_ARANGO_DOCUMENT_NOT_FOUND;
        TRI_V8_THROW_EXCEPTION(res);
      }

      res = subtrx.finish(res);
    }

    {
      // Collect all edges
      SingleCollectionReadOnlyTransaction subtrx2(new V8TransactionContext(true), vocbase, edgeCollectionCId);

      res = subtrx2.begin();

      if (res != TRI_ERROR_NO_ERROR) {
        TRI_V8_THROW_EXCEPTION(res);
      }

      bool error = false;

      uint32_t const n = static_cast<uint32_t>(edges.size());
      documents = v8::Array::New(isolate, static_cast<int>(n));
      for (size_t j = 0;  j < n;  ++j) {
        v8::Handle<v8::Value> doc = TRI_WrapShapedJson(isolate, subtrx2, edgeCollectionCId, edges[j].getDataPtr());

        if (doc.IsEmpty()) {
          error = true;
          break;
        }
        else {
          documents->Set(static_cast<uint32_t>(j), doc);
        }
      }
      if (error) {
        TRI_V8_THROW_EXCEPTION_MEMORY();
      }

      res = subtrx2.finish(res);
    }
  } 
  res = trx.finish(res);

  // Not yet correct. Needs to build an object first.
  // TRI_V8_RETURN(result);
  TRI_V8_RETURN(documents);
}
