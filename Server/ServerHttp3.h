#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <nghttp3/nghttp3.h>
#include <msquic.h>
#include <nlohmann/json.hpp>
#include "ReceiverInterface.h"

using json = nlohmann::json;

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
    HQUIC stream{};
    const QUIC_API_TABLE* ms_quic{};

    std::string method, path, authority, scheme;

    json request;
    std::string response;
    std::vector<std::string> name_store;
    std::vector<std::string> value_store;
    std::vector<nghttp3_nv>  nvs;
    uint64_t app_consumed = 0;
    uint64_t credit_reported = 0;

    std::string content_type = "application/json";
    size_t send_offset;

    // http3와 연결되어있는지 표시
    bool user_data_set = false;

    // void HttpWriteData(int64_t StreamId, const uint8_t* Data, size_t DataLen, QUICClient* Quic) {
    //     StreamElement* Stream = Quic->StreamMap->GetStreamById(StreamId);
    //     if(!Stream) {
    //         printf("[http3] Stream is null\n");
    //         return;
    //     }



    // }
};

class ServerHttp3 : public ReceiverInterface {
private:

    nghttp3_conn*                   http3_connection;

    int64_t                         control_stream_id;
    int64_t                         qpack_enc_stream_id;
    int64_t                         qpack_dec_stream_id;

    static int OnBeginHeaders(nghttp3_conn* conn, int64_t stream_id,
                                void* conn_user_data, void* stream_user_data);

    static int OnRecvHeader(nghttp3_conn* conn, int64_t stream_id, int32_t token,
                            nghttp3_rcbuf* name, nghttp3_rcbuf* value,
                            uint8_t flags, void* conn_user_data, void* stream_user_data);

    static int OnEndHeaders(nghttp3_conn* conn, int64_t stream_id, int fin,
                            void* conn_user_data, void* stream_user_data);

    static int OnRecvData(nghttp3_conn* conn, int64_t stream_id, const uint8_t* data,
                            size_t len, void* conn_user_data, void* stream_user_data);

    static int OnDeferredConsume(nghttp3_conn* conn, int64_t stream_id, size_t consumed,
                                void* conn_user_data, void* stream_user_data);

    static int OnEndStream(nghttp3_conn* conn, int64_t stream_id,
                            void* conn_user_data, void* stream_user_data);

    static int OnStreamClose(nghttp3_conn* conn, int64_t stream_id, uint64_t app_err,
                            void* conn_user_data, void* stream_user_data);

    static int OnAckedStreamData(nghttp3_conn* conn, int64_t stream_id, uint64_t data_len,
                                    void* conn_user_data, void* stream_user_data);

    static int OnStopSending(nghttp3_conn* conn, int64_t stream_id, uint64_t app_err,
                            void* conn_user_data, void* stream_user_data);

    static int OnResetStream(nghttp3_conn* conn, int64_t stream_id, uint64_t app_err,
                            void* conn_user_data, void* stream_user_data);

    static void OnRand(uint8_t* dest, size_t dest_len);

    static nghttp3_ssize ReadData(nghttp3_conn *conn, int64_t stream_id, nghttp3_vec *vec, size_t vec_cnt, 
                            uint32_t *pflags, void *conn_user_data, void *stream_user_data);

public:
    ServerHttp3(_In_ void* server_connection_context);
    ~ServerHttp3();

    void SetContext(int64_t stream_id, StreamCtx* stream_ctx);

    long int Read(int64_t stream_id, QUIC_STREAM_EVENT* event);

    bool SetControlStreamId(int64_t control_stream_id);
    bool SetQPACKStreamId(int64_t qpack_enc_stream_id, int64_t qpack_dec_stream_id);
};