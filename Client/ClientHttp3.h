#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <nghttp3/nghttp3.h>
#include <msquic.h>
#include "Http.h"

struct H3Header {
    nghttp3_rcbuf*      name;
    nghttp3_rcbuf*      value;
    int32_t             token;
    int64_t             flags;
    H3Header(nghttp3_rcbuf* name, nghttp3_rcbuf* value, int32_t token, int64_t flags) : name(name), value(value), token(token), flags(flags) {}
    ~H3Header() {
        if(name) nghttp3_rcbuf_decref(name);
        if(value) nghttp3_rcbuf_decref(value);
    }
};

struct StreamCtx {
    std::string method, path, status;
    std::vector<H3Header> headers;

    uint64_t app_consumed = 0;
    uint64_t credit_reported = 0;

    // void HttpWriteData(int64_t stream_id, const uint8_t* Data, size_t DataLen, QUICClient* Quic) {
    //     StreamElement* Stream = Quic->StreamMap->GetStreamById(stream_id);
    //     if(!Stream) {
    //         printf("[http3] Stream is null\n");
    //         return;
    //     }



    // }
};

class ClientHttp3 {
private:

    nghttp3_conn*                       http3_connection;

    const char*                         target_domain;
    const uint16_t                      target_port;
    const char*                         authority;

    const int64_t                       control_stream_id;
    const int64_t                       qpack_enc_stream_id;
    const int64_t                       qpack_dec_stream_id;

    std::atomic_flag                    pumping = ATOMIC_FLAG_INIT;

    // 헤더 섹션 시작 시 호출
    // 헤더 초기화
    static int OnBeginHeaders(nghttp3_conn* conn, int64_t stream_id,
                                void* conn_user_data, void* stream_user_data) {
        // ((StreamCtx*)stream_user_data)->headers.clear(); 
        printf("[http3] On Begin Headers\n");
        return 0;
    }

    // 단일 HTTP 헤더 수신 시  호출
    static int OnRecvHeader(nghttp3_conn* conn, int64_t stream_id, int32_t token,
                            nghttp3_rcbuf* name, nghttp3_rcbuf* value,
                            uint8_t flags, void* conn_user_data, void* stream_user_data) {
        ((StreamCtx*)stream_user_data)->headers.push_back(H3Header(name, value, token, flags));
        nghttp3_rcbuf_incref(name);
        nghttp3_rcbuf_incref(value);
        printf("[http3] On Recv Header\n");
        return 0;
    }

    // 헤더 섹션 종료 시 호출
    static int OnEndHeaders(nghttp3_conn* conn, int64_t stream_id, int fin,
                            void* conn_user_data, void* stream_user_data) {
        printf("[http3] On End Headers\n");
        return 0;
    }

    static int OnRecvData(nghttp3_conn* conn, int64_t stream_id, const uint8_t* data,
                            size_t len, void* conn_user_data, void* stream_user_data) {
        // 바디 조각 수신. 애플리케이션이 실제로 처리 완료한 만큼
        // QUIC 플로우컨트롤 크레딧을 복구해야 함(예: MsQuic StreamReceiveComplete).

        

        printf("[http3] On Recv Data\n");
        return 0;
    }

    static int OnDeferredConsume(nghttp3_conn* conn, int64_t stream_id, size_t consumed,
                                void* conn_user_data, void* stream_user_data) {
    // 동기화 지연으로 지금 ‘소비’된 바이트 공지.
    // Consumed 만큼 추가로 크레딧 복구.
    printf("[http3] On Deffered Consume\n");
    return 0;
    }

    static int OnEndStream(nghttp3_conn* conn, int64_t stream_id,
                            void* conn_user_data, void* stream_user_data) {
    // 수신 측 스트림이 모두 끝남(서버=요청 완수, 클라=응답 완수)
    printf("[http3] On End Stream\n");
    return 0;
    }

    static int OnStreamClose(nghttp3_conn* conn, int64_t stream_id, uint64_t app_err,
                            void* conn_user_data, void* stream_user_data) {
    // 스트림 종료(정상/리셋 등). 정리 작업.
    printf("[http3] On Strean Close\n");
    return 0;
    }

    static int OnAckedStreamData(nghttp3_conn* conn, int64_t stream_id, uint64_t data_len,
                                    void* conn_user_data, void* stream_user_data) {
    // 내가 보낸 바디 중 n바이트가 원격에서 ACK 됨 (버퍼 해제 타이밍 등)
    printf("[http3] On Acked Stream Data\n");
    return 0;
    }

    static int OnStopSending(nghttp3_conn* conn, int64_t stream_id, uint64_t app_err,
                            void* conn_user_data, void* stream_user_data) {
    // 라이브러리가 이 스트림에 STOP_SENDING 보내달라고 요청
    printf("[http3] On Stop Sending\n");
    return 0;
    }

    static int OnResetStream(nghttp3_conn* conn, int64_t stream_id, uint64_t app_err,
                            void* conn_user_data, void* stream_user_data) {
    // 라이브러리가 이 스트림을 RESET_STREAM 하라고 요청
    printf("[http3] On Reset Stream\n");
    return 0;
    }

    static void OnRand(uint8_t* dest, size_t dest_len) {
        printf("[http3] On Rand\n");
    }

    static nghttp3_ssize ReadData(nghttp3_conn *conn, int64_t stream_id, nghttp3_vec *vec, size_t vec_cnt, uint32_t *pflags, void *conn_user_data, void *stream_user_data) {
        printf("[http3] Read Data Callback\n");
        *pflags = NGHTTP3_DATA_FLAG_EOF;
        return 0;
    }

    struct QUIC_BUFFER_CONVERT {
        QUIC_BUFFER* buffers;
        size_t bytes;
    };

    static QUIC_BUFFER_CONVERT NgVec2QuicBuffer(nghttp3_vec* vecs, nghttp3_ssize vec_cnt) {
        QUIC_BUFFER* buffers = (QUIC_BUFFER*)malloc(sizeof(QUIC_BUFFER) * (size_t)vec_cnt);
        size_t bytes = 0;

        for(nghttp3_ssize i = 0; i < vec_cnt; ++i) {
            buffers[i].Buffer = vecs[i].base;
            buffers[i].Length = (uint32_t)vecs[i].len;
            bytes += vecs[i].len;
        }

        return {buffers, bytes};
    }

public:
    ClientHttp3(_In_ void* client_connection_context, int64_t control_stream_id, int64_t qpack_enc_stream_id, int64_t qpack_dec_stream_id, _In_z_ const char* target_doamin, uint16_t target_port);
    ~ClientHttp3();

    void Send(int64_t stream_id, void* quic_connection);
    
};