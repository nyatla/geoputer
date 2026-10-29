#pragma once
#include "DatasetTypes.h"
#include "../../../utils/BorrowedLayout.h"
namespace geoputer {
// Only a completed Storage writer may publish this UI-owned result.
using DatasetReadLayout=BorrowedLayout<DatasetView>;
static_assert(sizeof(DatasetView)<=map::BoundaryBytes,"Dataset result fits the existing UI back image");
inline bool validDatasetMemory(const DatasetRequest& request) {
    return request.token && request.screen_generation && request.buffer.id &&
        DatasetReadLayout::fits(request.buffer.data,request.buffer.capacity);
}
inline DatasetView* datasetResultMemory(const DatasetRequest& request) {
    return DatasetReadLayout::get(request.buffer.data,request.buffer.capacity);
}
inline DatasetView* beginDatasetMemory(const DatasetRequest& request) {
    return DatasetReadLayout::construct(request.buffer.data,request.buffer.capacity);
}
inline void endDatasetMemory(const DatasetRequest& request) {DatasetReadLayout::destroy(request.buffer.data,request.buffer.capacity);}
} // namespace geoputer
