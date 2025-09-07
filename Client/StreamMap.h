#pragma once
#include <iostream>
#include <unordered_map>
#include <msquic.h>

enum class StreamStatus : uint8_t {
    Idle,
    Open,
    HalfClosedLocal,
    HalfClosedRemote,
    Closed,
    Error
};

struct StreamElement {
    HQUIC stream = nullptr;
    StreamStatus status;
    StreamElement(_In_ HQUIC stream, StreamStatus status) : stream(stream), status(status) {}
};

class StreamMap {
private:
    const QUIC_API_TABLE*                               ms_quic;
    std::unordered_map<uint64_t, StreamElement>         map;

public:
    StreamMap(_In_ const QUIC_API_TABLE* ms_quic)
    : ms_quic(ms_quic)
    {

    }
    ~StreamMap() {
        map.clear();
    }

    StreamElement* GetStreamById(uint64_t stream_id) {
        return &map.at(stream_id);
    }
    StreamElement* GetStreamByHQUIC(_In_ HQUIC stream) {
        return GetStreamById(GetStreamId(stream));
    }

    void InsertStream(uint64_t stream_id, _In_ StreamElement stream) {
        map.insert({stream_id, stream});
    }

    void EraseStreamById(uint64_t stream_id) {
        map.erase(stream_id);
    }

    void EraseStreamByHQUIC(_In_ HQUIC stream) {
        EraseStreamById(GetStreamId(stream));
    } 

    uint64_t GetStreamId(_In_ HQUIC stream) {
        // 스트림 아이디에서 0번 비트는 발신자를 의미하고 1번 비트는 단,양방향 통신을 의미한다.
        uint64_t stream_id = 0;
        uint32_t size = sizeof(stream_id);
        ms_quic->GetParam(stream, QUIC_PARAM_STREAM_ID, &size, &stream_id);
        return stream_id;
    }

    bool IsBidirectionStream(_In_ HQUIC stream) {
        
        return (GetStreamId(stream) & 0x2) == 0;
    }

    void Print() {
        for (auto it = map.begin(); it != map.end(); ++it) {
            std::cout << " {" << it->first << " : " << it->second.stream << "} ";
        }
        printf("\n");
    }
};