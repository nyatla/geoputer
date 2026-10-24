// Diagnostic-only rejected experiment, 2026-10-24. Insert inside galuchat::detail.
template <class Raster>
inline void paintRleRun(Raster& dest, const Rect& target, int dest_x, int dest_y,
                        int resolution, int mode, int position, int count, int64_t value) {
    const bool vertical = (mode & 2) != 0;
    const int fixed_begin = vertical ? target.x : target.y;
    const int moving_begin = vertical ? target.y : target.x;
    const int64_t fixed_end = int64_t(fixed_begin) + (vertical ? target.width : target.height);
    const int64_t moving_end = int64_t(moving_begin) + (vertical ? target.height : target.width);
    // Stream rows are contiguous even for transposed serpentine traversal.
    // Clip whole rows before splitting the run, without consuming input here.
    const int first_outer = std::max(0, fixed_begin);
    const int last_outer = static_cast<int>(std::min<int64_t>(resolution, fixed_end));
    if (first_outer >= last_outer || moving_begin >= resolution || moving_end <= 0) return;
    const int first_row = mode == 3 ? resolution - last_outer : first_outer;
    const int last_row = mode == 3 ? resolution - first_outer : last_outer;
    const int end_position = std::min(position + count, last_row * resolution);
    position = std::max(position, first_row * resolution);
    count = end_position - position;
    while (count > 0) {
        const int row = position / resolution, column = position % resolution;
        const int outer = mode == 3 ? resolution - 1 - row : row;
        const bool reverse = bool(outer & 1) != (mode == 1);
        const int segment = std::min(count, resolution - column);
        const int low = reverse ? resolution - column - segment : column;
        const int high = low + segment;
        const int begin = std::max(low, moving_begin);
        const int end = static_cast<int>(std::min<int64_t>(high, moving_end));
        if (begin < end) {
            const int inner = reverse ? end - 1 : begin;
            const int x = vertical ? outer : inner, y = vertical ? inner : outer;
            const int direction = reverse ? -1 : 1;
            dest.setSpan(x - target.x + dest_x, y - target.y + dest_y, end - begin,
                         vertical ? 0 : direction, vertical ? direction : 0, value);
        }
        position += segment;
        count -= segment;
    }
}
