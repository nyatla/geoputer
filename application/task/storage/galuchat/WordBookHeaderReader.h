#pragma once
#include "FixedHeader.h"
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace geoputer {
struct WordBookHeader {
    struct Range {uint32_t offset=0,bytes=0;};
    Range gw,tm,tt,ti;
    uint32_t tokens=0,token_pages=0,tm_payload_bytes=0;
    uint32_t components=0,page_size=0,text_pages=0,token_bits=0,tt_payload_bytes=0;
    uint32_t depth=0,records=0,code_bits=0,roots=0,stream_bytes=0;
};
// The SD Source owns the file and borrowed buffer. This reader keeps only
// bounded header values; GW00 metadata is framed and skipped, never retained.
class WordBookHeaderReader {
public:
    template<class Source> [[gnu::noinline]] static WordBookHeader read(Source& source) {
        WordBookHeader h;
        h.gw=range(source,"GW00");source.seek(end(h.gw));
        h.tm=range(source,"TM00");source.seek(end(h.tm));
        h.tt=range(source,"TT00");source.seek(end(h.tt));
        h.ti=range(source,"TI00");source.seek(end(h.ti));
        require(source.atEnd());
        source.seek(h.gw.offset);
        {
            galuchat::detail::LimitedReader body(source,h.gw.bytes);
            require(matchesFixedAscii16(body,"GisWordBook/0"));
            const auto bytes=number(body);require(bytes==body.remaining());
        }
        source.seek(h.tm.offset);
        {
            galuchat::detail::LimitedReader body(source,h.tm.bytes);
            h.tokens=number(body);h.token_pages=number(body);h.tm_payload_bytes=numberRemaining(body);
        }
        source.seek(h.tt.offset);
        {
            galuchat::detail::LimitedReader body(source,h.tt.bytes);
            h.components=number(body);h.page_size=number(body);h.text_pages=number(body);h.token_bits=number(body);
            require(h.token_bits>=1 && h.token_bits<=16);h.tt_payload_bytes=numberRemaining(body);
        }
        source.seek(h.ti.offset);
        {
            galuchat::detail::LimitedReader body(source,h.ti.bytes);
            h.depth=number(body);h.records=number(body);h.code_bits=number(body);h.roots=number(body);h.stream_bytes=number(body);
            require(h.depth && h.records && h.code_bits>=1 && h.code_bits<=31 && h.roots && h.stream_bytes);
            require(h.stream_bytes==body.remaining());
        }
        // Preserve the SDK constructor's cross-table bit-capacity checks.
        require(uint64_t(h.tokens)<=(uint64_t{1}<<h.token_bits));
        require(uint64_t(h.components)<=(uint64_t{1}<<h.code_bits));
        return h;
    }
private:
    static void require(bool ok) {if(!ok)throw std::runtime_error("Invalid Wordbook header");}
    static uint32_t number(galuchat::ByteReader& reader) {
        const auto value=reader.readMbUInt();require(value<=UINT32_MAX);return static_cast<uint32_t>(value);
    }
    static uint32_t numberRemaining(const galuchat::detail::LimitedReader& reader) {
        require(reader.remaining()<=UINT32_MAX);return static_cast<uint32_t>(reader.remaining());
    }
    static uint32_t end(const WordBookHeader::Range& range) {return range.offset+range.bytes;}
    template<class Source> static WordBookHeader::Range range(Source& source,const char* expected) {
        char tag[4];for(auto& byte:tag)byte=static_cast<char>(source.readByte());
        require(std::memcmp(tag,expected,sizeof(tag))==0);
        const auto bytes=number(source);const auto offset=source.pos();
        require(offset<=source.length() && bytes<=source.length()-offset);
        return {static_cast<uint32_t>(offset),bytes};
    }
};
} // namespace geoputer
