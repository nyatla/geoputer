#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "wgsmap3_reader.hpp"

namespace galuchat {

/** GisWordBook/0 reader without persistent page or hierarchical indexes. */
class GaluchatGisWordBookReader {
public:
    explicit GaluchatGisWordBookReader(
        const std::vector<uint8_t>& bytes,
        size_t token_cache_size = 64)
        : GaluchatGisWordBookReader(
            std::make_shared<BufferReaderFactory>(bytes), token_cache_size) {}

    GaluchatGisWordBookReader(
        const uint8_t* bytes,
        size_t size,
        size_t token_cache_size = 64)
        : GaluchatGisWordBookReader(
            std::make_shared<BufferReaderFactory>(bytes, size), token_cache_size) {}

    explicit GaluchatGisWordBookReader(
        std::shared_ptr<const ReaderFactory> factory,
        size_t token_cache_size = 64)
        : factory_(std::move(factory)), token_cache_size_(token_cache_size) {
        std::array<ChunkLocation, 4> chunks = readChunkLayout(*factory_);
        token_chunk_ = chunks[1];
        text_chunk_ = chunks[2];
        index_chunk_ = chunks[3];

        {
            std::unique_ptr<ByteReader> reader = factory_->create(chunks[0].data_start);
            detail::LimitedReader chunk(*reader, chunks[0].size);
            readHeader(chunk, chunks[0].size);
        }
        {
            std::unique_ptr<ByteReader> reader = factory_->create(token_chunk_.data_start);
            detail::LimitedReader chunk(*reader, token_chunk_.size);
            token_count_ = TokenMapReader(chunk).tokenCount();
        }
        int text_token_bits;
        {
            std::unique_ptr<ByteReader> reader = factory_->create(text_chunk_.data_start);
            detail::LimitedReader chunk(*reader, text_chunk_.size);
            TextTableReader text(chunk);
            component_count_ = text.recordCount();
            text_token_bits = text.tokenBits();
        }
        int index_code_bits;
        {
            std::unique_ptr<ByteReader> reader = factory_->create(index_chunk_.data_start);
            detail::LimitedReader chunk(*reader, index_chunk_.size);
            HierarchicalIndexReader index(chunk, index_chunk_.size);
            record_count_ = index.recordCount();
            depth_ = index.depth();
            index_code_bits = index.codeBits();
        }
        if (text_token_bits < requiredBits(token_count_)) {
            throw std::runtime_error("TT00 token_bits is smaller than TM00 requires");
        }
        if (index_code_bits < requiredBits(component_count_)) {
            throw std::runtime_error("TI00 code_bits is smaller than TT00 requires");
        }
    }

    size_t recordCount() const { return record_count_; }
    size_t depth() const { return depth_; }
    size_t componentCount() const { return component_count_; }
    const std::string& metadata() const { return metadata_; }

    std::vector<size_t> readCodeSet(size_t index) const {
        std::unique_ptr<ByteReader> reader = factory_->create(index_chunk_.data_start);
        detail::LimitedReader chunk(*reader, index_chunk_.size);
        return HierarchicalIndexReader(chunk, index_chunk_.size).readCodeSet(index);
    }

    std::string readComponent(size_t code) const {
        std::vector<std::pair<size_t, std::string>> values =
            readComponents(std::vector<size_t>{code});
        return values[0].second;
    }

    std::vector<std::string> readStringSet(size_t index) const {
        std::vector<size_t> codes = readCodeSet(index);
        std::vector<std::pair<size_t, std::string>> components = readComponents(codes);
        std::vector<std::string> result;
        result.reserve(codes.size());
        for (size_t code : codes) result.push_back(findValue(components, code));
        return result;
    }

    bool readStringSetByCode(int64_t code, std::vector<std::string>& result) const {
        result.clear();
        if (code <= 0) return false;
        result = readStringSet(static_cast<size_t>(code - 1));
        return true;
    }

    // Opt-in entry points for external indexes. Existing readers remain sequential.
    struct IndexRootRange {
        size_t depth, record_count;
        int code_bits;
        size_t start_bit, bit_count;
        bool final_root = false;
    };
    static std::vector<size_t> readCodeSetFromRoot(ByteReader& source,
                                                  const IndexRootRange& range, size_t target) {
        if (source.bitOffset() || range.start_bit > 7 || !range.bit_count
            || !range.depth || range.depth > range.bit_count) {
            throw std::runtime_error("invalid indexed TI00 root range");
        }
        const size_t end_bits = detail::addSize(range.start_bit, range.bit_count, "TI00 root bits");
        detail::LimitedReader bytes(source, detail::addSize(end_bits, 7, "TI00 root bytes") / 8);
        bytes.skipBits(range.start_bit);
        HierarchicalIndexReader root(bytes, range);
        auto result = root.readIndexedRoot(target, end_bits, range.final_root);
        if (bitPosition(bytes) > end_bits) throw std::runtime_error("TI00 root range exceeded");
        return result;
    }

    // Caller-owned variant; the existing vector API remains available.
    static size_t readCodeSetFromRootInto(ByteReader& source,const IndexRootRange& range,
                                         size_t target,uint32_t* output,size_t capacity) {
        if (!output || capacity<range.depth) throw std::length_error("TI00 output capacity exceeded");
        if (source.bitOffset() || range.start_bit>7 || !range.bit_count || !range.depth || range.depth>range.bit_count)
            throw std::runtime_error("invalid indexed TI00 root range");
        const size_t end_bits=detail::addSize(range.start_bit,range.bit_count,"TI00 root bits");
        detail::LimitedReader bytes(source,detail::addSize(end_bits,7,"TI00 root bytes")/8);
        bytes.skipBits(range.start_bit);
        HierarchicalIndexReader root(bytes,range);
        root.readIndexedRootInto(target,end_bits,range.final_root,output);
        if (bitPosition(bytes)>end_bits) throw std::runtime_error("TI00 root range exceeded");
        return range.depth;
    }

    // Decode a single TT00 record into a bounded caller-owned token array.
    // Capacity applies to this component, not the dataset or the entire path.
    static size_t readTokenIdsFromPageInto(ByteReader& source,size_t page_bytes,
        size_t first_code,size_t record_count,int token_bits,size_t target,
        uint16_t* output,size_t capacity) {
        if (source.bitOffset() || token_bits<1 || token_bits>16 || !record_count || !output)
            throw std::runtime_error("invalid indexed TT00 page");
        const auto end=detail::addSize(first_code,record_count,"TT00 page end");
        if (target<first_code || target>=end) throw std::runtime_error("TT00 target outside page");
        detail::LimitedReader page(source,page_bytes);
        if (page.readByte()!=0) throw std::runtime_error("unsupported TT00 page header");
        const auto tokens=detail::toSize(page.readMbUInt(),"TT00 record tokens");
        const auto records=detail::toSize(page.readMbUInt(),"TT00 page records");
        const auto stream_bytes=detail::toSize(page.readMbUInt(),"TT00 stream size");
        if (records!=record_count || !stream_bytes || stream_bytes!=page.remaining())
            throw std::runtime_error("TT00 indexed page length/count mismatch");
        if (tokens>capacity) throw std::length_error("Wordbook token capacity exceeded");
        detail::LimitedReader stream(page,stream_bytes);
        const auto packets=detail::toSize(stream.readMbUInt(),"TT00 packet count");
        std::array<int64_t,PALETTE_SIZE+1> palette{};palette.fill(-1);
        size_t record=0,used=0;target-=first_code;
        for (size_t packet=0;packet<packets;++packet) {
            const int type=static_cast<int>(stream.readBitsAsInt32(2));
            const auto bits=detail::toSize(stream.readMbUInt(),"TT00 packet bits");
            if (type==0) {
                const auto start=bitPosition(stream);const auto slots=stream.readBitsAsInt32(PALETTE_SIZE);
                palette.fill(-1);
                for (int bit=0;bit<PALETTE_SIZE;++bit)
                    if (slots&(1<<bit)) palette[bit+1]=stream.readBitsAsInt32(token_bits);
                assertConsumed(stream,start,bits);
            } else if (type==1) {
                const auto start=bitPosition(stream);const auto slot=stream.readBitsAsInt32(4);
                if (!slot) throw std::runtime_error("TT00 palette slot zero is reserved");
                palette[slot]=stream.readBitsAsInt32(token_bits);assertConsumed(stream,start,bits);
            } else if (type==2) {
                if (bits%4) throw std::runtime_error("invalid TT00 payload size");
                for (size_t i=0;i<bits/4;++i) {
                    const auto local=stream.readBitsAsInt32(4);
                    if (!local) {
                        if (record==target) {
                            if (used!=tokens) throw std::runtime_error("TT00 record token count mismatch");
                            return used;
                        }
                        ++record;
                    } else if (record==target) {
                        if (palette[local]<0) throw std::runtime_error("TT00 empty palette slot");
                        if (used>=tokens) throw std::runtime_error("TT00 record token count mismatch");
                        output[used++]=static_cast<uint16_t>(palette[local]);
                    }
                }
            } else stream.skipBits(bits);
        }
        throw std::runtime_error("TT00 target record terminator not found");
    }

    // source starts at PageHeader; only the selected page is visible to decoding.
    static std::vector<std::pair<size_t, std::vector<size_t>>> readTokenIdSetsFromPage(
        ByteReader& source, size_t page_bytes, size_t first_code, size_t record_count,
        int token_bits, const std::vector<size_t>& targets) {
        if (source.bitOffset() || token_bits < 1 || token_bits > 16 || !record_count)
            throw std::runtime_error("invalid indexed TT00 page");
        const size_t end = detail::addSize(first_code, record_count, "TT00 page end");
        validateIndexedTargets(targets, first_code, end);
        if (targets.empty()) return {};
        detail::LimitedReader page(source, page_bytes);
        if (page.readByte() != 0) throw std::runtime_error("unsupported TT00 page header");
        const size_t tokens = detail::toSize(page.readMbUInt(), "TT00 record tokens");
        const size_t records = detail::toSize(page.readMbUInt(), "TT00 page records");
        const size_t stream_bytes = detail::toSize(page.readMbUInt(), "TT00 stream size");
        validateTextPage(tokens, records, 0, false);
        if (records != record_count || !stream_bytes || stream_bytes != page.remaining())
            throw std::runtime_error("TT00 indexed page length/count mismatch");
        std::vector<size_t> local;
        for (size_t code : targets) local.push_back(code - first_code);
        detail::LimitedReader stream(page, stream_bytes);
        auto result = decodeTargets(stream, records, local, token_bits);
        for (auto& value : result) {
            if (value.second.size() != tokens) throw std::runtime_error("TT00 record token count mismatch");
            value.first += first_code;
        }
        return result;
    }

    // source starts at a fixed-width TokenStream range, without the page header.
    static std::vector<std::pair<size_t, std::string>> readTokensFromPage(
        ByteReader& source, size_t first_token, size_t count, size_t token_bytes,
        const std::vector<size_t>& targets) {
        if (source.bitOffset() || !token_bytes || !count)
            throw std::runtime_error("invalid indexed TM00 page");
        const size_t end = detail::addSize(first_token, count, "TM00 page end");
        validateIndexedTargets(targets, first_token, end);
        detail::LimitedReader stream(source, detail::multiplySize(count, token_bytes, "TM00 stream bytes"));
        std::vector<std::pair<size_t, std::string>> result;
        size_t consumed = 0;
        for (size_t token : targets) {
            const size_t offset = detail::multiplySize(token - first_token, token_bytes, "TM00 token offset");
            stream.skipInByte(offset - consumed);
            result.push_back({token, detail::readAscii(stream, token_bytes)});
            consumed = offset + token_bytes;
        }
        return result;
    }

private:
    static void validateIndexedTargets(const std::vector<size_t>& targets, size_t first, size_t end) {
        for (size_t i = 0; i < targets.size(); ++i) {
            if (targets[i] < first || targets[i] >= end || (i && targets[i] <= targets[i-1]))
                throw std::runtime_error("indexed targets must be sorted, unique and in range");
        }
    }
    static constexpr int PALETTE_SIZE = 15;

    struct ChunkLocation {
        std::string name;
        size_t data_start = 0;
        size_t size = 0;
    };

    struct IndexHeader {
        size_t max_depth = 0;
        size_t record_count = 0;
        int code_bits = 0;
        size_t root_count = 0;
        size_t stream_size = 0;
    };

    static std::array<ChunkLocation, 4> readChunkLayout(const ReaderFactory& factory) {
        static const std::array<const char*, 4> names{{"GW00", "TM00", "TT00", "TI00"}};
        std::array<ChunkLocation, 4> chunks{};
        std::unique_ptr<ByteReader> reader = factory.create();
        for (size_t i = 0; i < names.size(); i++) {
            std::string name = detail::readAscii(*reader, 4);
            size_t size = detail::toSize(
                reader->readMbUInt(), "GisWordBook chunk size");
            if (name != names[i]) throw std::runtime_error("invalid GisWordBook chunk order");
            size_t start = reader->pos();
            chunks[i] = {name, start, size};
            reader->skipInByte(size);
        }
        if (!reader->atEnd()) {
            throw std::runtime_error("GisWordBook has trailing bytes");
        }
        return chunks;
    }

    void readHeader(ByteReader& reader, size_t size) {
        size_t start = reader.pos();
        if (detail::readBStrAscii(reader, 16) != "GisWordBook/0") {
            throw std::runtime_error("unsupported GisWordBook version");
        }
        size_t metadata_size = detail::toSize(
            reader.readMbUInt(), "GW00 metadata size");
        metadata_ = detail::readAscii(reader, metadata_size);
        if (reader.pos() - start != size) throw std::runtime_error("GW00 has trailing bytes");
    }

    class TokenMapReader {
    public:
        explicit TokenMapReader(ByteReader& reader) : reader_(reader) {
            token_count_ = detail::toSize(reader_.readMbUInt(), "TM00 token count");
            page_count_ = detail::toSize(reader_.readMbUInt(), "TM00 page count");
        }

        size_t tokenCount() const { return token_count_; }

        std::vector<std::pair<size_t, std::string>> readTokens(
            const std::vector<size_t>& targets) {
            std::vector<std::pair<size_t, std::string>> result;
            result.reserve(targets.size());
            size_t target_pos = 0;
            size_t token_base = 0;
            size_t previous = 0;
            bool has_previous = false;
            for (size_t page = 0; page < page_count_; page++) {
                if (reader_.readByte() != 0) throw std::runtime_error("unsupported TM00 page header");
                size_t token_size = detail::toSize(reader_.readMbUInt(), "TM00 token size");
                size_t page_tokens = detail::toSize(reader_.readMbUInt(), "TM00 page token count");
                size_t stream_size = detail::toSize(reader_.readMbUInt(), "TM00 stream size");
                validateTokenPage(token_size, page_tokens, stream_size, previous, has_previous);
                previous = token_size;
                has_previous = true;
                size_t page_end = detail::addSize(
                    token_base, page_tokens, "TM00 page end");
                size_t consumed = 0;
                while (target_pos < targets.size() && targets[target_pos] < page_end) {
                    size_t token_id = targets[target_pos];
                    if (token_id < token_base || token_id >= token_count_) {
                        throw std::runtime_error("TM00 token id out of range");
                    }
                    size_t offset = detail::multiplySize(
                        token_id - token_base, token_size, "TM00 token offset");
                    reader_.skipInByte(offset - consumed);
                    result.push_back({token_id, detail::readAscii(reader_, token_size)});
                    consumed = offset + token_size;
                    target_pos++;
                }
                if (target_pos == targets.size()) return result;
                reader_.skipInByte(stream_size - consumed);
                token_base = page_end;
            }
            throw std::runtime_error("TM00 token id out of range");
        }

    private:
        ByteReader& reader_;
        size_t token_count_ = 0;
        size_t page_count_ = 0;
    };

    class TextTableReader {
    public:
        explicit TextTableReader(ByteReader& reader) : reader_(reader) {
            record_count_ = detail::toSize(reader_.readMbUInt(), "TT00 record count");
            (void)reader_.readMbUInt();
            page_count_ = detail::toSize(reader_.readMbUInt(), "TT00 page count");
            token_bits_ = detail::toInt(reader_.readMbUInt(), "TT00 token_bits");
            if (token_bits_ < 1 || token_bits_ > 16) {
                throw std::runtime_error("invalid TT00 token_bits");
            }
        }

        size_t recordCount() const { return record_count_; }
        int tokenBits() const { return token_bits_; }

        std::vector<std::pair<size_t, std::vector<size_t>>> readTokenIdSets(
            const std::vector<size_t>& codes) {
            for (size_t code : codes) {
                if (code >= record_count_) throw std::runtime_error("TT00 code out of range");
            }
            std::vector<std::pair<size_t, std::vector<size_t>>> result;
            size_t target_pos = 0;
            size_t record_base = 0;
            size_t previous = 0;
            bool has_previous = false;
            for (size_t page = 0; page < page_count_; page++) {
                if (reader_.readByte() != 0) throw std::runtime_error("unsupported TT00 page header");
                size_t record_tokens = detail::toSize(reader_.readMbUInt(), "TT00 record token count");
                size_t page_records = detail::toSize(reader_.readMbUInt(), "TT00 page record count");
                size_t stream_size = detail::toSize(reader_.readMbUInt(), "TT00 stream size");
                validateTextPage(record_tokens, page_records, previous, has_previous);
                previous = record_tokens;
                has_previous = true;
                size_t page_end = detail::addSize(
                    record_base, page_records, "TT00 page end");
                size_t target_start = target_pos;
                while (target_pos < codes.size() && codes[target_pos] < page_end) target_pos++;
                if (target_start == target_pos) {
                    reader_.skipInByte(stream_size);
                    record_base = page_end;
                    continue;
                }
                std::vector<size_t> local_targets;
                for (size_t i = target_start; i < target_pos; i++) {
                    local_targets.push_back(codes[i] - record_base);
                }
                size_t stream_start = bitPosition(reader_);
                std::vector<std::pair<size_t, std::vector<size_t>>> local =
                    decodeTargets(reader_, page_records, local_targets, token_bits_);
                for (auto& entry : local) {
                    if (entry.second.size() != record_tokens) {
                        throw std::runtime_error("TT00 record token count mismatch");
                    }
                    entry.first += record_base;
                    result.push_back(std::move(entry));
                }
                if (target_pos == codes.size()) return result;
                size_t consumed = bitPosition(reader_) - stream_start;
                size_t stream_bits = detail::multiplySize(
                    stream_size, size_t{8}, "TT00 stream bits");
                if (consumed > stream_bits) {
                    throw std::runtime_error("TT00 packet exceeds stream size");
                }
                reader_.skipBits(stream_bits - consumed);
                record_base = page_end;
            }
            throw std::runtime_error("TT00 code out of range");
        }

    private:
        ByteReader& reader_;
        size_t record_count_ = 0;
        size_t page_count_ = 0;
        int token_bits_ = 0;
    };

    class HierarchicalIndexReader {
    public:
        HierarchicalIndexReader(ByteReader& reader, size_t size) : reader_(reader) {
            size_t start = reader_.pos();
            header_.max_depth = detail::toSize(reader_.readMbUInt(), "TI00 max depth");
            header_.record_count = detail::toSize(reader_.readMbUInt(), "TI00 record count");
            header_.code_bits = detail::toInt(reader_.readMbUInt(), "TI00 code_bits");
            header_.root_count = detail::toSize(reader_.readMbUInt(), "TI00 root count");
            header_.stream_size = detail::toSize(reader_.readMbUInt(), "TI00 stream size");
            validateIndexHeader(header_);
            size_t header_size = reader_.pos() - start;
            if (header_size > size || header_.stream_size != size - header_size) {
                throw std::runtime_error("TI00 has trailing bytes");
            }
        }

        // Metadata-only construction for an independently positioned root.
        HierarchicalIndexReader(ByteReader& reader, const IndexRootRange& range) : reader_(reader) {
            header_ = {range.depth, range.record_count, range.code_bits, 1, 1};
            validateIndexHeader(header_);
        }
        std::vector<size_t> readIndexedRoot(size_t target, size_t end_bits, bool final_root) {
            if (target >= header_.record_count) throw std::runtime_error("TI00 index out of range");
            std::vector<size_t> result(header_.max_depth, 0);
            readIndexedRootInto(target,end_bits,final_root,result);
            return result;
        }

        template<class Output>
        void readIndexedRootInto(size_t target,size_t end_bits,bool final_root,Output& result) {
            if (target>=header_.record_count) throw std::runtime_error("TI00 index out of range");
            if (header_.max_depth == 1) {
                const size_t leaves = detail::toSize(reader_.readMbUInt(), "TI00 root leaves");
                if (leaves != header_.record_count) throw std::runtime_error("TI00 indexed root count mismatch");
                const size_t payload = detail::multiplySize(leaves, size_t(header_.code_bits), "TI00 root payload");
                if (bitPosition(reader_) > end_bits || (payload > end_bits - bitPosition(reader_) ||
                    (final_root ? end_bits - bitPosition(reader_) - payload > 7 : payload != end_bits - bitPosition(reader_))))
                    throw std::runtime_error("TI00 indexed root length mismatch");
                reader_.skipBits(detail::multiplySize(target, size_t(header_.code_bits), "TI00 leaf offset"));
                result[0] = reader_.readBitsAsInt32(header_.code_bits);
            } else {
                result[0] = reader_.readBitsAsInt32(header_.code_bits);
                const size_t leaves = detail::toSize(reader_.readMbUInt(), "TI00 root leaves");
                const size_t payload = detail::toSize(reader_.readMbUInt(), "TI00 root payload");
                if (leaves != header_.record_count || bitPosition(reader_) > end_bits
                    || (payload && (payload > end_bits - bitPosition(reader_) ||
                    (final_root ? end_bits - bitPosition(reader_) - payload > 7 : payload != end_bits - bitPosition(reader_)))))
                    throw std::runtime_error("TI00 indexed root length/count mismatch");
                readChildren(1, leaves, target, result);
            }
        }

        size_t recordCount() const { return header_.record_count; }
        size_t depth() const { return header_.max_depth; }
        int codeBits() const { return header_.code_bits; }

        std::vector<size_t> readCodeSet(size_t target) {
            if (target >= header_.record_count) throw std::runtime_error("TI00 index out of range");
            std::vector<size_t> result(header_.max_depth, 0);
            if (header_.max_depth == 1) readLeafBlocksByCount(header_.root_count, target, result);
            else readPrefixesByCount(0, header_.root_count, target, result);
            return result;
        }

    private:
        template<class Output>
        void readPrefixesByCount(
            size_t depth, size_t count, size_t target, Output& out) {
            for (size_t i = 0; i < count; i++) {
                size_t code = reader_.readBitsAsInt32(header_.code_bits);
                size_t leaves = detail::toSize(reader_.readMbUInt(), "TI00 leaves");
                size_t payload_bits = detail::toSize(reader_.readMbUInt(), "TI00 payload bits");
                if (target < leaves) {
                    out[depth] = code;
                    readChildren(depth + 1, leaves, target, out);
                    return;
                }
                skipPayload(depth + 1, leaves, payload_bits);
                target -= leaves;
            }
            throw std::runtime_error("TI00 index not found");
        }

        template<class Output>
        void readChildren(
            size_t depth, size_t leaves, size_t target, Output& out) {
            if (depth == header_.max_depth - 1) {
                readLeafBlocksByLeafCount(leaves, target, out);
                return;
            }
            size_t consumed = 0;
            while (consumed < leaves) {
                size_t code = reader_.readBitsAsInt32(header_.code_bits);
                size_t child_leaves = detail::toSize(reader_.readMbUInt(), "TI00 child leaves");
                size_t payload_bits = detail::toSize(reader_.readMbUInt(), "TI00 payload bits");
                size_t next = detail::addSize(consumed, child_leaves, "TI00 leaf count");
                if (target < next) {
                    out[depth] = code;
                    readChildren(depth + 1, child_leaves, target - consumed, out);
                    return;
                }
                skipPayload(depth + 1, child_leaves, payload_bits);
                consumed = next;
            }
            throw std::runtime_error("TI00 child index not found");
        }

        template<class Output>
        void readLeafBlocksByCount(
            size_t blocks, size_t target, Output& out) {
            for (size_t block = 0; block < blocks; block++) {
                size_t leaves = detail::toSize(reader_.readMbUInt(), "TI00 block leaves");
                if (target < leaves) {
                    reader_.skipBits(detail::multiplySize(
                        target, static_cast<size_t>(header_.code_bits), "TI00 leaf offset"));
                    out[header_.max_depth - 1] = reader_.readBitsAsInt32(header_.code_bits);
                    return;
                }
                reader_.skipBits(detail::multiplySize(
                    leaves, static_cast<size_t>(header_.code_bits), "TI00 leaf payload"));
                target -= leaves;
            }
            throw std::runtime_error("TI00 leaf index not found");
        }

        template<class Output>
        void readLeafBlocksByLeafCount(
            size_t leaves, size_t target, Output& out) {
            size_t consumed = 0;
            while (consumed < leaves) {
                size_t block_leaves = detail::toSize(reader_.readMbUInt(), "TI00 block leaves");
                size_t next = detail::addSize(consumed, block_leaves, "TI00 leaf count");
                if (target < next) {
                    reader_.skipBits(detail::multiplySize(
                        target - consumed,
                        static_cast<size_t>(header_.code_bits),
                        "TI00 leaf offset"));
                    out[header_.max_depth - 1] = reader_.readBitsAsInt32(header_.code_bits);
                    return;
                }
                reader_.skipBits(detail::multiplySize(
                    block_leaves,
                    static_cast<size_t>(header_.code_bits),
                    "TI00 leaf payload"));
                consumed = next;
            }
            throw std::runtime_error("TI00 leaf index not found");
        }

        void skipPayload(size_t depth, size_t leaves, size_t payload_bits) {
            if (payload_bits > 0) reader_.skipBits(payload_bits);
            else skipChildren(depth, leaves);
        }

        void skipChildren(size_t depth, size_t leaves) {
            size_t consumed = 0;
            if (depth == header_.max_depth - 1) {
                while (consumed < leaves) {
                    size_t block_leaves = detail::toSize(reader_.readMbUInt(), "TI00 block leaves");
                    reader_.skipBits(detail::multiplySize(
                        block_leaves,
                        static_cast<size_t>(header_.code_bits),
                        "TI00 leaf payload"));
                    consumed = detail::addSize(consumed, block_leaves, "TI00 leaf count");
                }
                return;
            }
            while (consumed < leaves) {
                (void)reader_.readBitsAsInt32(header_.code_bits);
                size_t child_leaves = detail::toSize(reader_.readMbUInt(), "TI00 child leaves");
                size_t payload_bits = detail::toSize(reader_.readMbUInt(), "TI00 payload bits");
                skipPayload(depth + 1, child_leaves, payload_bits);
                consumed = detail::addSize(consumed, child_leaves, "TI00 leaf count");
            }
        }

        ByteReader& reader_;
        IndexHeader header_;
    };

    std::vector<std::pair<size_t, std::string>> readComponents(
        std::vector<size_t> codes) const {
        std::sort(codes.begin(), codes.end());
        codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
        std::vector<std::pair<size_t, std::vector<size_t>>> token_sets;
        {
            std::unique_ptr<ByteReader> reader = factory_->create(text_chunk_.data_start);
            detail::LimitedReader chunk(*reader, text_chunk_.size);
            token_sets = TextTableReader(chunk).readTokenIdSets(codes);
        }
        std::vector<size_t> token_ids;
        for (const auto& entry : token_sets) {
            token_ids.insert(token_ids.end(), entry.second.begin(), entry.second.end());
        }
        std::sort(token_ids.begin(), token_ids.end());
        token_ids.erase(std::unique(token_ids.begin(), token_ids.end()), token_ids.end());

        std::vector<std::pair<size_t, std::string>> resolved;
        std::vector<size_t> missing;
        for (size_t token_id : token_ids) {
            std::string token;
            if (cachedToken(token_id, token)) resolved.push_back({token_id, token});
            else missing.push_back(token_id);
        }
        if (!missing.empty()) {
            std::unique_ptr<ByteReader> reader = factory_->create(token_chunk_.data_start);
            detail::LimitedReader chunk(*reader, token_chunk_.size);
            std::vector<std::pair<size_t, std::string>> loaded =
                TokenMapReader(chunk).readTokens(missing);
            resolved.insert(resolved.end(), loaded.begin(), loaded.end());
        }

        std::vector<std::pair<size_t, std::string>> result;
        for (size_t code : codes) {
            const std::vector<size_t>& ids = findValue(token_sets, code);
            std::string value;
            for (size_t token_id : ids) {
                const std::string& token = findValue(resolved, token_id);
                value += token;
                rememberToken(token_id, token);
            }
            result.push_back({code, std::move(value)});
        }
        return result;
    }

    bool cachedToken(size_t token_id, std::string& token) const {
        for (size_t i = 0; i < token_cache_.size(); i++) {
            if (token_cache_[i].first != token_id) continue;
            token = token_cache_[i].second;
            auto entry = token_cache_[i];
            token_cache_.erase(token_cache_.begin() + static_cast<std::ptrdiff_t>(i));
            token_cache_.push_back(std::move(entry));
            return true;
        }
        return false;
    }

    void rememberToken(size_t token_id, const std::string& token) const {
        if (token_cache_size_ == 0) return;
        for (auto it = token_cache_.begin(); it != token_cache_.end(); ++it) {
            if (it->first == token_id) {
                token_cache_.erase(it);
                break;
            }
        }
        token_cache_.push_back({token_id, token});
        if (token_cache_.size() > token_cache_size_) token_cache_.erase(token_cache_.begin());
    }

    static std::vector<std::pair<size_t, std::vector<size_t>>> decodeTargets(
        ByteReader& reader,
        size_t,
        const std::vector<size_t>& targets,
        int token_bits) {
        size_t packets = detail::toSize(reader.readMbUInt(), "TT00 packet count");
        std::array<int64_t, PALETTE_SIZE + 1> palette{};
        palette.fill(-1);
        std::vector<std::pair<size_t, std::vector<size_t>>> result;
        std::vector<size_t> current;
        size_t record = 0;
        size_t target_pos = 0;
        size_t target = targets[0];
        for (size_t packet = 0; packet < packets; packet++) {
            int type = static_cast<int>(reader.readBitsAsInt32(2));
            size_t value_bits = detail::toSize(reader.readMbUInt(), "TT00 packet bits");
            if (type == 0) {
                size_t start = bitPosition(reader);
                int slots = static_cast<int>(reader.readBitsAsInt32(PALETTE_SIZE));
                palette.fill(-1);
                for (int bit = 0; bit < PALETTE_SIZE; bit++) {
                    if ((slots & (1 << bit)) != 0) {
                        palette[static_cast<size_t>(bit + 1)] = reader.readBitsAsInt32(token_bits);
                    }
                }
                assertConsumed(reader, start, value_bits);
            } else if (type == 1) {
                size_t start = bitPosition(reader);
                size_t slot = reader.readBitsAsInt32(4);
                if (slot == 0) throw std::runtime_error("TT00 palette slot zero is reserved");
                palette[slot] = reader.readBitsAsInt32(token_bits);
                assertConsumed(reader, start, value_bits);
            } else if (type == 2) {
                if (value_bits % 4 != 0) throw std::runtime_error("invalid TT00 payload size");
                for (size_t i = 0; i < value_bits / 4; i++) {
                    size_t local = reader.readBitsAsInt32(4);
                    if (local == 0) {
                        if (record == target) {
                            result.push_back({target, std::move(current)});
                            current.clear();
                            target_pos++;
                            if (target_pos == targets.size()) return result;
                            target = targets[target_pos];
                        }
                        record++;
                    } else if (record == target) {
                        if (palette[local] < 0) throw std::runtime_error("TT00 empty palette slot");
                        current.push_back(static_cast<size_t>(palette[local]));
                    }
                }
            } else {
                reader.skipBits(value_bits);
            }
        }
        throw std::runtime_error("TT00 target record terminator not found");
    }

    static void validateTokenPage(
        size_t token_size, size_t count, size_t stream_size,
        size_t previous, bool has_previous) {
        if ((has_previous && token_size < previous) || token_size == 0 || count == 0
            || stream_size != detail::multiplySize(token_size, count, "TM00 stream size")) {
            throw std::runtime_error("invalid TM00 token page");
        }
    }

    static void validateTextPage(
        size_t token_count, size_t record_count, size_t previous, bool has_previous) {
        if ((has_previous && token_count < previous) || record_count == 0) {
            throw std::runtime_error("invalid TT00 text page");
        }
    }

    static void validateIndexHeader(const IndexHeader& value) {
        if (value.max_depth == 0 || value.record_count == 0
            || value.code_bits < 1 || value.code_bits > 31
            || value.root_count == 0 || value.stream_size == 0) {
            throw std::runtime_error("invalid TI00 header");
        }
    }

    static int requiredBits(size_t count) {
        int bits = 1;
        while (bits < 63 && (uint64_t{1} << bits) < count) bits++;
        return bits;
    }

    static size_t bitPosition(const ByteReader& reader) {
        return detail::multiplySize(reader.pos(), size_t{8}, "bit position")
            - static_cast<size_t>(reader.bitOffset());
    }

    static void assertConsumed(ByteReader& reader, size_t start, size_t expected) {
        if (bitPosition(reader) - start != expected) {
            throw std::runtime_error("TT00 packet size mismatch");
        }
    }

    template <class Value>
    static const Value& findValue(
        const std::vector<std::pair<size_t, Value>>& values,
        size_t key) {
        for (const auto& entry : values) {
            if (entry.first == key) return entry.second;
        }
        throw std::runtime_error("decoded value not found");
    }

    std::shared_ptr<const ReaderFactory> factory_;
    ChunkLocation token_chunk_;
    ChunkLocation text_chunk_;
    ChunkLocation index_chunk_;
    size_t token_count_ = 0;
    size_t component_count_ = 0;
    size_t record_count_ = 0;
    size_t depth_ = 0;
    std::string metadata_;
    size_t token_cache_size_ = 0;
    mutable std::vector<std::pair<size_t, std::string>> token_cache_;
};

} // namespace galuchat
