#pragma once

#include <iostream>
#include <msquic.h>
#include <nghttp3/nghttp3.h>
#include "StreamMap.h"

enum class StreamTypes : uint8_t {
        BIDIRECTION,
        UNIDIRECTION
    };

class ConnectionContext {
private:
    StreamMap*                      stream_map;

    const HQUIC                     quic_connection;

    uint64_t                        http3_control_stream_id;
    uint64_t                        http3_qpack_encoder_stream_id;
    uint64_t                        http3_qpack_decoder_stream_id;
    bool                            http3_stream_ok = false;

    // _In_을 붙이는 이유는 읽기 전용으로 인자를 넘긴다는 뜻이다. Microsoft Source-code Annotation Language(SAL) 참고
    static
    _IRQL_requires_max_(DISPATCH_LEVEL)
    _Function_class_(QUIC_STREAM_CALLBACK)
    QUIC_STATUS
    QUIC_API
    ServerStreamCallback(_In_ HQUIC stream, _In_opt_ void* context, _Inout_ QUIC_STREAM_EVENT* event);

    HQUIC GetStream(StreamTypes stream_type);
    bool OpenStream(StreamTypes stream_type, uint64_t& stream_id);

    inline bool IsBidirectionStream(_In_ HQUIC stream) {
        // 스트림 아이디에서 0번 비트는 발신자를 의미하고 1번 비트는 단,양방향 통신을 의미한다.
        return (GetStreamId(stream) & 0x2) == 0;
    }

    inline uint64_t GetStreamId(_In_ HQUIC stream) 
    { 
        uint64_t stream_id = 0;
        uint32_t size = sizeof(stream_id);
        ms_quic->GetParam(stream, QUIC_PARAM_STREAM_ID, &size, &stream_id);
        return stream_id;
    }
public:
    const QUIC_API_TABLE*           ms_quic;
    ConnectionContext(_In_ const QUIC_API_TABLE* ms_quic, _In_ HQUIC quic_connection);
    ~ConnectionContext();
    bool ConnectionContextInit();

    // 클라이언트와 연결되었을 때(QUIC handshake 종료 후) 수행할 동작 정의
    static
    _IRQL_requires_max_(DISPATCH_LEVEL)
    _Function_class_(QUIC_CONNECTION_CALLBACK)
    QUIC_STATUS
    QUIC_API
    ServerConnectionCallback(_In_ HQUIC connection, _In_opt_ void* context, _Inout_ QUIC_CONNECTION_EVENT* event);

};