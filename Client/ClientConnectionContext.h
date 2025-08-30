#pragma once

#include <iostream>
#include <msquic.h>
#include <nghttp3/nghttp3.h>
#include "StreamMap.h"

enum class StreamTypes : uint8_t {
        BIDIRECTION,
        UNIDIRECTION
    };
    
//log 기록을 위한 환경변수명. 동작을 위해서는 같은 이름의 환경변수를 OS에 등록해주어야한다.
constexpr char                      ssl_key_log_env_var[] = "SSLKEYLOGFILE";

class ClientConnectionContext {
private:
    const QUIC_API_TABLE*           ms_quic;
    StreamMap*                      stream_map;

    QUIC_TLS_SECRETS                client_secrets;
    const HQUIC                     quic_connection;
    bool                            connection_ok = false;

    uint64_t                        http3_control_stream_id;
    uint64_t                        http3_qpack_encoder_stream_id;
    uint64_t                        http3_qpack_decoder_stream_id;
    bool                            http3_stream_ok = false;

    // Datagram 전송 최대 크기
    uint32_t                        max_send_length;
    bool                            datagram_send_enabled;

    const char*                     resumption_ticket_string = NULL;
    const char*                     ssl_key_log_file = getenv(ssl_key_log_env_var);

    const char*                     target_domain;
    const uint16_t                  target_port;

    // 클라이언트와 연결되었을 때(QUIC handshake 종료 후) 수행할 동작 정의
    static
    _IRQL_requires_max_(DISPATCH_LEVEL)
    _Function_class_(QUIC_CONNECTION_CALLBACK)
    QUIC_STATUS
    QUIC_API
    ClientConnectionCallback(_In_ HQUIC connection, _In_opt_ void* context, _Inout_ QUIC_CONNECTION_EVENT* event);

    // _In_을 붙이는 이유는 읽기 전용으로 인자를 넘긴다는 뜻이다. Microsoft Source-code Annotation Language(SAL) 참고
    static
    _IRQL_requires_max_(DISPATCH_LEVEL)
    _Function_class_(QUIC_STREAM_CALLBACK)
    QUIC_STATUS
    QUIC_API
    ClientStreamCallback(_In_ HQUIC stream, _In_opt_ void* context, _Inout_ QUIC_STREAM_EVENT* event);

    HQUIC GetConnection(_In_ HQUIC quic_registration, _In_ HQUIC quic_configuration, _In_z_ const char *target, _In_range_(1, 65535) uint16_t port);
    HQUIC GetStream(StreamTypes stream_type);
    bool OpenStream(StreamTypes stream_type, uint64_t& stream_id);

    void StreamSend(_In_ HQUIC stream, uint8_t* data, size_t length, bool fin);
    void DatagramSend(_In_ HQUIC connection, uint8_t* data, size_t length);

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

    void WriteSslKeyLogFile(
        _In_z_ const char* file_name,
        _In_ QUIC_TLS_SECRETS* tls_secrets
    );

    void
    EncodeHexBuffer(
        _In_reads_(BufferLen) uint8_t* buffer,
        _In_ uint8_t buffer_len,
        _Out_writes_bytes_(2*buffer_len) char* hex_string
    );

public:
    ClientConnectionContext(_In_ const QUIC_API_TABLE* ms_quic, _In_ HQUIC quic_registration, _In_ HQUIC quic_configuration, _In_z_ const char* target_domain, _In_range_(1, 65535) uint16_t target_port);
    ~ClientConnectionContext();
    bool Http3Init();
    void Post(uint8_t* data, size_t len, bool fin) {
        StreamSend(GetStream(StreamTypes::BIDIRECTION), data, len, fin);
    }

    void ConnectionShutdown(bool send_notify, QUIC_UINT62 error_code);
    inline bool IsConnectionOk() {return connection_ok;}
};