// RAW decoder before row traversal (2026-10-24). Used only for comparison.
    template <class Raster>
    void readRawRect(
        CellHeader header,
        ByteReader& reader,
        int resolution,
        const PalletValueReader& pallet_reader,
        PalletMgr& pmgr,
        const Rect& target,
        Raster& dest,
        int dest_x,
        int dest_y) const {
        if (!header.hasValidRawReservedBits()) {
            throw std::runtime_error("RAW CellHeader reserved bits are not zero");
        }
        int pallet_size = header.palletResolution() == CellHeader::PALLET_NBIT ? 0 : readRawPallet(header, reader, pallet_reader, pmgr);
        int bits = pallet_size == 0 ? 0 : bitWidthForPaletteSize(pallet_size);
        int pixels = resolution * resolution;
        for (int index = 0; index < pixels; index++) {
            int64_t value = pallet_size == 0 ? static_cast<int64_t>(reader.readMbUInt())
                                             : pmgr.getValue(static_cast<int>(reader.readBitsAsInt32(bits)));
            int lx = index % resolution;
            int ly = index / resolution;
            if (detail::insideRect(target, lx, ly)) {
                dest.set(lx - target.x + dest_x, ly - target.y + dest_y, value);
            }
        }
    }

