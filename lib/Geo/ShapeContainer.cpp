////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2017-2018 ArangoDB GmbH, Cologne, Germany
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
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#include "ShapeContainer.h"

#include <s2/s2cap.h>
#include <s2/s2cell.h>
#include <s2/s2latlng_rect.h>
#include <s2/s2metrics.h>
#include <s2/s2multipoint_region.h>
#include <s2/s2multipolyline.h>
#include <s2/s2point_region.h>
#include <s2/s2polygon.h>
#include <s2/s2region.h>
#include <s2/s2region_coverer.h>

#include <velocypack/Iterator.h>
#include <velocypack/Slice.h>
#include <velocypack/velocypack-aliases.h>

#include "Basics/voc-errors.h"
#include "Geo/GeoParams.h"
#include "Logger/Logger.h"

using namespace arangodb;
using namespace arangodb::geo;

Result ShapeContainer::parseCoordinates(VPackSlice const& json, bool geoJson) {
  TRI_ASSERT(_data == nullptr);
  if (!json.isArray() || json.length() < 2) {
    return Result(TRI_ERROR_BAD_PARAMETER, "Invalid coordinate pair");
  }

  VPackSlice lat = json.at(geoJson ? 1 : 0);
  VPackSlice lng = json.at(geoJson ? 0 : 1);
  if (!lat.isNumber() || !lng.isNumber()) {
    return Result(TRI_ERROR_BAD_PARAMETER, "Invalid coordinate pair");
  }

  resetCoordinates(lat.getNumber<double>(), lng.getNumber<double>());
  return TRI_ERROR_NO_ERROR;
}

ShapeContainer::ShapeContainer(ShapeContainer&& other) noexcept
    : _data(other._data), _type(other._type) {
  other._data = nullptr;
  other._type = ShapeContainer::Type::EMPTY;
}
ShapeContainer::~ShapeContainer() { delete _data; }

void ShapeContainer::reset(std::unique_ptr<S2Region>&& ptr, Type tt) noexcept {
  delete _data;
  _type = tt;
  _data = ptr.release();
}

void ShapeContainer::reset(S2Region* ptr, Type tt) noexcept {
  delete _data;
  _type = tt;
  _data = ptr;
}

void ShapeContainer::resetCoordinates(double lat, double lon) {
  delete _data;
  _type = ShapeContainer::Type::S2_POINT;
  _data = new S2PointRegion(S2LatLng::FromDegrees(lat, lon).ToPoint());
}

S2Point ShapeContainer::centroid() const noexcept {
  switch (_type) {
    case ShapeContainer::Type::S2_POINT: {
      return (static_cast<S2PointRegion const*>(_data))->point();
    }
    case ShapeContainer::Type::S2_POLYLINE: {
      return (static_cast<S2Polyline const*>(_data))->GetCentroid();
    }
    case ShapeContainer::Type::S2_LATLNGRECT: {
      return static_cast<S2LatLngRect const*>(_data)->GetCentroid();
    }
    case ShapeContainer::Type::S2_POLYGON: {
      return (static_cast<S2Polygon const*>(_data))->GetCentroid();
    }
    case ShapeContainer::Type::S2_MULTIPOINT: {
      S2MultiPointRegion const* pts =
          (static_cast<S2MultiPointRegion const*>(_data));
      S2Point c(0, 0, 0);
      for (int k = 0; k < pts->num_points(); k++) {
        c += pts->point(k);
      }
      c = (c / pts->num_points());
      c.Norm();
      return c; // FIXME probably broken
    }
    case ShapeContainer::Type::S2_MULTIPOLYLINE: {
      S2MultiPolyline const* lines =
          (static_cast<S2MultiPolyline const*>(_data));
      S2Point c(0, 0, 0);
      for (size_t k = 0; k < lines->num_lines(); k++) {
        c += lines->line(k).GetCentroid();
      }
      c /= lines->num_lines();
      c.Norm();
      return c;
    }

    case ShapeContainer::Type::EMPTY:
      TRI_ASSERT(false);
      return S2Point();
  }
}

std::vector<S2CellId> ShapeContainer::covering(S2RegionCoverer* coverer) const
    noexcept {
  TRI_ASSERT(coverer != nullptr && _data != nullptr);

  std::vector<S2CellId> cover;
  switch (_type) {
    case ShapeContainer::Type::S2_POINT: {
      S2Point const& c = (static_cast<S2PointRegion const*>(_data))->point();
      return {S2CellId(c)};
    }
    case ShapeContainer::Type::S2_POLYLINE:
    case ShapeContainer::Type::S2_LATLNGRECT:
    case ShapeContainer::Type::S2_POLYGON: {
      coverer->GetCovering(*_data, &cover);
      break;
    }
    case ShapeContainer::Type::S2_MULTIPOINT: {  // multi-optimization
      S2MultiPointRegion const* pts =
          (static_cast<S2MultiPointRegion const*>(_data));
      for (int k = 0; k < pts->num_points(); k++) {
        cover.emplace_back(S2CellId(pts->point(k)));
      }
      break;
    }
    case ShapeContainer::Type::S2_MULTIPOLYLINE: {  // multi-optimization
      S2MultiPolyline const* lines =
          (static_cast<S2MultiPolyline const*>(_data));
      for (size_t k = 0; k < lines->num_lines(); k++) {
        std::vector<S2CellId> tmp;
        coverer->GetCovering(*lines, &tmp);
        if (!tmp.empty()) {
          cover.reserve(cover.size() + tmp.size());
          cover.insert(cover.end(), tmp.begin(), tmp.end());
        }
      }
      break;
    }

    case ShapeContainer::Type::EMPTY:
      LOG_TOPIC(ERR, Logger::FIXME) << "Invalid GeoShape usage";
      TRI_ASSERT(false);
  }

  return cover;
}

double ShapeContainer::distanceFrom(S2Point const& other) const noexcept {
  return centroid().Angle(other) * geo::kEarthRadiusInMeters;
}

/// @brief may intersect cell
bool ShapeContainer::mayIntersect(S2CellId cell) const noexcept {
  TRI_ASSERT(_data != nullptr);
  return _data->MayIntersect(S2Cell(cell));
}

void ShapeContainer::updateBounds(QueryParams& qp) const noexcept {
  TRI_ASSERT(_data != nullptr);
  if (_data == nullptr) {
    return;
  }
  
  S2Point origin = this->centroid();
  S2LatLngRect rect = _data->GetRectBound();
  S2LatLng ll(origin);
  S1Angle a1(ll, rect.lo());
  S1Angle a2(ll, S2LatLng(rect.lat_lo(), rect.lng_hi()));
  S1Angle a3(ll, S2LatLng(rect.lat_hi(), rect.lng_lo()));
  S1Angle a4(ll, rect.hi());

  qp.origin = ll;
  qp.maxDistance = std::max(std::max(a1.radians(), a2.radians()),
                            std::max(a3.radians(), a4.radians())) *
                   kEarthRadiusInMeters;
}

bool ShapeContainer::contains(S2Point const& pp) const {
  if (_type == ShapeContainer::Type::EMPTY) {
    return false;
  }
  return _data->Contains(pp);
}

bool ShapeContainer::contains(S2Polyline const* other) const {
  switch (_type) {
    case ShapeContainer::Type::S2_POINT:
    case ShapeContainer::Type::S2_MULTIPOINT: {
      return false;  // rect.is_point() && rect.lo() == S2LatLng
    }

    case ShapeContainer::Type::S2_POLYLINE: {
      S2Polyline const* ll = static_cast<S2Polyline const*>(_data);
      return ll->ApproxEquals(*other, S1Angle::Radians(1e-6));
    }
      
    case ShapeContainer::Type::S2_LATLNGRECT: {
      S2LatLngRect const* rect = static_cast<S2LatLngRect const*>(_data);
      for (int k = 0; k < other->num_vertices(); k++) {
        if (!rect->Contains(other->vertex(k))) {
          return false;
        }
      }
      return true;
    }

    case ShapeContainer::Type::S2_POLYGON: {
      // FIXME this seems to be incomplete
      S2Polygon const* poly = static_cast<S2Polygon const*>(_data);
      auto cuts = poly->IntersectWithPolyline(*other);
      if (cuts.size() != 1) {
        return false;  // is clipping holes or no edge
      }
      // The line may be in the polygon
      return cuts[0]->NearlyCovers(*other, S1Angle::Degrees(1e-10));
    }

    case ShapeContainer::Type::S2_MULTIPOLYLINE: {
      S2MultiPolyline const* mpl = static_cast<S2MultiPolyline const*>(_data);
      for (size_t k = 0; k < mpl->num_lines(); k++) {
        if (mpl->line(k).ApproxEquals(*other, S1Angle::Degrees(1e-6))) {
          return true;
        }
      }
      return false;
    }

    case ShapeContainer::Type::EMPTY:
      TRI_ASSERT(false);
      return false;
  }
}

bool ShapeContainer::contains(S2LatLngRect const* other) const {
  switch (_type) {
    case ShapeContainer::Type::S2_POINT:
      if (other->is_point()) {
        return static_cast<S2PointRegion*>(_data)->point() == other->lo().ToPoint();
      }
      return false;
      
    case ShapeContainer::Type::S2_POLYLINE: {
      if (other->is_point()) {
        S2Point pp = other->lo().ToPoint();
        S2Polyline* pl = static_cast<S2Polyline*>(_data);
        for (int k = 0; k < pl->num_vertices(); k++) {
          if (pl->vertex(k) == pp) {
            return true;
          }
        }
      }
      return false;
    }
      
    case ShapeContainer::Type::S2_LATLNGRECT: {
      S2LatLngRect const* self = static_cast<S2LatLngRect const*>(_data);
      return self->Contains(*other);
    }
      
    case ShapeContainer::Type::S2_POLYGON: {
      S2Polygon const* poly = static_cast<S2Polygon const*>(_data);
      for (int k = 0; k < 4; k++) {
        if (!poly->Contains(other->GetVertex(k).ToPoint())) {
          return false;
        }
      }
      return true;
    }
      
    case ShapeContainer::Type::S2_MULTIPOINT: {
      if (other->is_point()) {
        S2Point pp = other->lo().ToPoint();
        S2MultiPointRegion* mpr =  static_cast<S2MultiPointRegion*>(_data);
        for (int k = 0; mpr->num_points() < k; k++) {
          if (mpr->point(k) == pp)  {
            return true;
          }
        }
      }
      return false;
    }
      
    case ShapeContainer::Type::S2_MULTIPOLYLINE: {
      if (other->is_point()) {
        S2Point pp = other->lo().ToPoint();
        S2MultiPolyline* mpl = static_cast<S2MultiPolyline*>(_data);
        for (size_t k = 0; k < mpl->num_lines(); k++) {
          S2Polyline const& pl = mpl->line(k);
          for (int n = 0; n < pl.num_vertices(); n++) {
            if (pl.vertex(n) == pp) {
              return true;
            }
          }
        }
      }
      return false;
    }
      
    case ShapeContainer::Type::EMPTY:
      TRI_ASSERT(false);
      return false;
  }
}


bool ShapeContainer::contains(S2Polygon const* poly) const {
  switch (_type) {
    case ShapeContainer::Type::S2_POINT:
    case ShapeContainer::Type::S2_MULTIPOINT: {
      return false;  // rect.is_point() && rect.lo() == S2LatLng
    }
    case ShapeContainer::Type::S2_POLYLINE:
    case ShapeContainer::Type::S2_MULTIPOLYLINE: {
      return false;  // numerically not well defined
    }
    case ShapeContainer::Type::S2_LATLNGRECT: {
      S2LatLngRect const* rect = static_cast<S2LatLngRect const*>(_data);
      for (int k = 0; k < poly->num_loops(); k++) {
        S2Loop const* loop = poly->loop(k);
        for (int n = 0; n < loop->num_vertices(); n++) {
          if (!rect->Contains(loop->vertex(n))) {
            return false;
          }
        }
      }
      return true;
    }
    case ShapeContainer::Type::S2_POLYGON: {
      return (static_cast<S2Polygon const*>(_data))->Contains(poly);
    }
    case ShapeContainer::Type::EMPTY:
      TRI_ASSERT(false);
      return false;
  }
}

bool ShapeContainer::contains(ShapeContainer const* cc) const {
  switch (cc->_type) {
    case ShapeContainer::Type::S2_POINT: {
      S2Point const& p = static_cast<S2PointRegion*>(cc->_data)->point();
      return _data->Contains(p);
    }
    case ShapeContainer::Type::S2_POLYLINE: {
      return contains(static_cast<S2Polyline const*>(cc->_data));
    }
    case ShapeContainer::Type::S2_LATLNGRECT: {
      return contains(static_cast<S2LatLngRect const*>(cc->_data));
    }
    case ShapeContainer::Type::S2_POLYGON: {
      return contains(static_cast<S2Polygon const*>(cc->_data));
    }
    case ShapeContainer::Type::S2_MULTIPOINT: {
      auto pts = static_cast<S2MultiPointRegion const*>(cc->_data);
      for (int k = 0; k < pts->num_points(); k++) {
        if (!_data->Contains(pts->point(k))) {
          return false;
        }
      }
      return true;
    }
    case ShapeContainer::Type::S2_MULTIPOLYLINE: {
      auto lines = static_cast<S2MultiPolyline const*>(cc->_data);
      for (size_t k = 0; k < lines->num_lines(); k++) {
        if (!this->contains(&(lines->line(k)))) {
          return false;
        }
      }
      return true;
    }

    case ShapeContainer::Type::EMPTY:
      TRI_ASSERT(false);
      return false;
  }
}

bool ShapeContainer::equals(Coordinate const* cc) const {
  geo::Coordinate centroid1 = this->centroid();
  double lat1 = centroid1.latitude;
  double lon1 = centroid1.longitude;

  double lat2 = cc->latitude;
  double lon2 = cc->longitude;

  if (lat1 == lat2 && lon1 == lon2) {
    return true;
  }

  return false;
}

bool ShapeContainer::equals(Coordinate const& point, Coordinate const& other) const {
  if (point.latitude == other.latitude && point.longitude == other.longitude) {
    return true;
  }
 
  return false;
}

bool ShapeContainer::equals(double lat2, double lon2) const {
  geo::Coordinate centroid1 = this->centroid();
  double lat1 = centroid1.latitude;
  double lon1 = centroid1.longitude;

  if (lat1 == lat2 && lon1 == lon2) {
    return true;
  }

  return false;
}

bool ShapeContainer::equals(S2Polyline const* other) const {
  S2Polyline const* ll = static_cast<S2Polyline const*>(_data);
  return ll->Equals(other);
}

bool ShapeContainer::equals(S2Polyline const* poly, S2Polyline const* other) const {
  return poly->Equals(other);
}

bool ShapeContainer::equals(S2LatLngRect const* other) const {
  S2LatLngRect const* llrect = static_cast<S2LatLngRect const*>(_data);
  return llrect->ApproxEquals(*other);
}

bool ShapeContainer::equals(S2Polygon const* other) const {
  S2Polygon const* poly = static_cast<S2Polygon const*>(_data);
  return poly->Equals(other);
}

bool ShapeContainer::equals(ShapeContainer const* cc) const {
  bool result = false;

  // equals function will only trigger for equal types
  // if types are not equal, result is false
  if (cc->_type != _type) {
    return result;
  }

  switch (cc->_type) {
    case ShapeContainer::Type::S2_POINT: {
      S2Point const& p = static_cast<S2PointRegion*>(cc->_data)->point();
      return equals(S2LatLng::Latitude(p).degrees(), S2LatLng::Longitude(p).degrees());
    }
    case ShapeContainer::Type::S2_POLYLINE: {
      return equals(static_cast<S2Polyline const*>(cc->_data));
    }
    case ShapeContainer::Type::S2_LATLNGRECT: {
      return equals(static_cast<S2LatLngRect const*>(cc->_data));
    }
    case ShapeContainer::Type::S2_POLYGON: {
      return equals(static_cast<S2Polygon const*>(cc->_data));
    }
    case ShapeContainer::Type::S2_MULTIPOINT: {
      auto pts1 = static_cast<S2MultiPointRegion const*>(_data);
      auto pts2 = static_cast<S2MultiPointRegion const*>(cc->_data);

      if (pts1->num_points() != pts2->num_points()) {
        return false;
      }

      for (int k = 0; k < pts1->num_points(); k++) {
        if (!equals(
              Coordinate(S2LatLng::Latitude(pts1->point(k)).degrees(),
                         S2LatLng::Longitude(pts1->point(k)).degrees()),
              Coordinate(S2LatLng::Latitude(pts2->point(k)).degrees(),
                         S2LatLng::Longitude(pts2->point(k)).degrees())
              )) {
          return false;
        }
      }
      return true;
    }
    case ShapeContainer::Type::S2_MULTIPOLYLINE: {
      auto lines1 = static_cast<S2MultiPolyline const*>(_data);
      auto lines2 = static_cast<S2MultiPolyline const*>(cc->_data);

      if (lines1->num_lines() != lines2->num_lines()) {
        return false;
      }

      for (size_t k = 0; k < lines2->num_lines(); k++) {
        if (!this->equals(&(lines1->line(k)), &(lines2->line(k)))) {
          return false;
        }
      }
      return true;
    }

    case ShapeContainer::Type::EMPTY:
      TRI_ASSERT(false);
      return false;
  }
}

bool ShapeContainer::intersects(geo::Coordinate const* cc) const {
  return contains(cc);  // same
}

bool ShapeContainer::intersects(S2Polyline const* other) const {
  switch (_type) {
    case ShapeContainer::Type::S2_POINT: {
      S2Point const& p = static_cast<S2PointRegion*>(_data)->point();
      // containment is only numerically defined on the endpoints
      for (int k = 0; k < other->num_vertices(); k++) {
        if (other->vertex(k) == p) {
          return true;
        }
      }
      return false;
    }
    case ShapeContainer::Type::S2_POLYLINE: {
      S2Polyline const* ll = static_cast<S2Polyline const*>(_data);
      return ll->Intersects(other);
    }
    case ShapeContainer::Type::S2_LATLNGRECT: {
      S2LatLngRect const* rect = static_cast<S2LatLngRect const*>(_data);
      for (int k = 0; k < other->num_vertices(); k++) {
        if (rect->Contains(other->vertex(k))) {
          return true;
        }
      }
      return false;
    }
    case ShapeContainer::Type::S2_POLYGON: {
      S2Polygon const* poly = static_cast<S2Polygon const*>(_data);
      auto cuts = poly->IntersectWithPolyline(*other);
      return !cuts.empty();
    }
    case ShapeContainer::Type::S2_MULTIPOINT:
    case ShapeContainer::Type::S2_MULTIPOLYLINE:
    case ShapeContainer::Type::EMPTY:
      TRI_ASSERT(false);
      return false;
  }
}

bool ShapeContainer::intersects(S2LatLngRect const* other) const {
  switch (_type) {
    case ShapeContainer::Type::S2_POINT:
    case ShapeContainer::Type::S2_POLYLINE:
      return contains(other); // same
      
    case ShapeContainer::Type::S2_LATLNGRECT: {
      S2LatLngRect const* self = static_cast<S2LatLngRect const*>(_data);
      return self->Intersects(*other);
    }
      
    case ShapeContainer::Type::S2_POLYGON: {
      S2Polygon const* poly = static_cast<S2Polygon const*>(_data);
      for (int k = 0; k < 4; k++) {
        if (poly->Contains(other->GetVertex(k).ToPoint())) {
          return true;
        }
      }
      return false;
    }
      
    case ShapeContainer::Type::S2_MULTIPOINT:
    case ShapeContainer::Type::S2_MULTIPOLYLINE: {
      return contains(other); // same
    }
      
    case ShapeContainer::Type::EMPTY:
      TRI_ASSERT(false);
      return false;
  }
}


bool ShapeContainer::intersects(S2Polygon const* poly) const {
  switch (_type) {
    case ShapeContainer::Type::S2_POINT: {
      S2Point const& p = static_cast<S2PointRegion*>(_data)->point();
      return poly->Contains(p);
    }
    case ShapeContainer::Type::S2_POLYLINE: {
      LOG_TOPIC(ERR, Logger::FIXME)
          << "intersection with polyline is not well defined";
      return false;  // numerically not well defined
    }
    case ShapeContainer::Type::S2_LATLNGRECT: {
      S2LatLngRect const* rect = static_cast<S2LatLngRect const*>(_data);
      for (int k = 0; k < 4; k++) {
        if (poly->Contains(rect->GetVertex(k).ToPoint())) {
          return true;
        }
      }
      return false;
    }
    case ShapeContainer::Type::S2_POLYGON: {
      S2Polygon const* self = static_cast<S2Polygon const*>(_data);
      return self->Intersects(poly);
    }
    case ShapeContainer::Type::EMPTY:
    case ShapeContainer::Type::S2_MULTIPOINT:
    case ShapeContainer::Type::S2_MULTIPOLYLINE:
      TRI_ASSERT(false);
      return false;
  }
}

bool ShapeContainer::intersects(ShapeContainer const* cc) const {
  switch (cc->_type) {
    case ShapeContainer::Type::S2_POINT: {
      S2Point const& p = static_cast<S2PointRegion*>(cc->_data)->point();
      return _data->Contains(p);  // same
    }
    case ShapeContainer::Type::S2_POLYLINE: {
      return intersects(static_cast<S2Polyline const*>(cc->_data));
    }
    case ShapeContainer::Type::S2_POLYGON: {
      return intersects(static_cast<S2Polygon const*>(cc->_data));
    }
    case ShapeContainer::Type::S2_LATLNGRECT: {
      return intersects(static_cast<S2LatLngRect const*>(cc->_data));
    }
    case ShapeContainer::Type::S2_MULTIPOINT: {
      auto pts = static_cast<S2MultiPointRegion const*>(cc->_data);
      for (int k = 0; k < pts->num_points(); k++) {
        if (_data->Contains(pts->point(k))) {
          return true;
        }
      }
      return false;
    }
    case ShapeContainer::Type::S2_MULTIPOLYLINE: {
      auto lines = static_cast<S2MultiPolyline const*>(cc->_data);
      for (size_t k = 0; k < lines->num_lines(); k++) {
        if (this->intersects(&(lines->line(k)))) {
          return true;
        }
      }
      return false;
    }
    case ShapeContainer::Type::EMPTY:
      TRI_ASSERT(false);
      return false;
  }
}
