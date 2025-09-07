#include "ServerHttp3.h"
#include "ServerConnectionContext.h"

ServerHttp3::ServerHttp3(_In_ void* server_connection_context)
{
    nghttp3_settings settings{};
    nghttp3_callbacks callbacks{};
    nghttp3_data_reader data_reader = {ReadData};

    callbacks.begin_headers    = &OnBeginHeaders;
    callbacks.recv_header      = &OnRecvHeader;
    callbacks.end_headers      = &OnEndHeaders;
    callbacks.recv_data        = &OnRecvData;
    callbacks.deferred_consume = &OnDeferredConsume;
    callbacks.end_stream       = &OnEndStream;
    callbacks.stream_close     = &OnStreamClose;
    callbacks.acked_stream_data= &OnAckedStreamData;
    callbacks.stop_sending     = &OnStopSending;
    callbacks.reset_stream     = &OnResetStream;
    callbacks.rand             = &OnRand;

    nghttp3_settings_default(&settings);
    if(nghttp3_conn_server_new(&http3_connection, &callbacks, &settings, NULL, server_connection_context)) {
        printf("[http3] Nghttp3 client new fail\n");
        return;
    }
}

ServerHttp3::~ServerHttp3()
{
    
}

// 헤더 섹션 시작 시 호출
// 헤더 초기화
int ServerHttp3::OnBeginHeaders(nghttp3_conn *conn, int64_t stream_id,
                          void *conn_user_data, void *stream_user_data)
{
    StreamCtx *stream_ctx = (StreamCtx *)stream_user_data;
    if (!stream_ctx)
    {
        ServerConnectionContext *connection_ctx = (ServerConnectionContext *)conn_user_data;
        stream_ctx = connection_ctx->SetHttp3Context(stream_id);
    }
    printf("Debug\n");   
    stream_ctx->request = json::object();
    // printf("Debug1\n");
    // stream_ctx->response.clear();
    // printf("Debug2\n");
    // stream_ctx->method.clear();
    // printf("Debug3\n");
    // stream_ctx->path.clear();
    // printf("Debug4\n");
    // stream_ctx->authority.clear();
    // printf("Debug5\n");
    // stream_ctx->scheme.clear();
    // printf("Debug6\n");
    stream_ctx->name_store.clear();
    printf("Debug7\n");
    stream_ctx->value_store.clear();
    printf("Debug8\n");
    stream_ctx->nvs.clear();
    printf("Debug9\n");

    stream_ctx->send_offset = 0;
    stream_ctx->app_consumed = 0;
    stream_ctx->credit_reported = 0;
    printf("[http3] On Begin Headers\n");
    return 0;
}

// 단일 HTTP 헤더 수신 시  호출
int ServerHttp3::OnRecvHeader(nghttp3_conn *conn, int64_t stream_id, int32_t token,
                        nghttp3_rcbuf *name, nghttp3_rcbuf *value,
                        uint8_t flags, void *conn_user_data, void *stream_user_data)
{
    StreamCtx *stream_ctx = (StreamCtx *)stream_user_data;

    nghttp3_vec nb = nghttp3_rcbuf_get_buf(name);
    nghttp3_vec vb = nghttp3_rcbuf_get_buf(value);
    printf("[h3] hdr: %.*s: %.*s\n", (int)nb.len, nb.base, (int)vb.len, vb.base);
    std::string n(nb.base, nb.base + nb.len);
    std::string v(vb.base, vb.base + vb.len);

    if (n == ":method")
        stream_ctx->method = v;
    else if (n == ":scheme")
        stream_ctx->scheme = v;
    else if (n == ":authority")
        stream_ctx->authority = v;
    else if (n == ":path")
        stream_ctx->path = v;
    else {
        stream_ctx->name_store.push_back(std::move(n));
        stream_ctx->value_store.push_back(std::move(v));
        nghttp3_nv nv;
        nv.name = (uint8_t*)n.c_str();
        nv.namelen = n.size();
        nv.value = (uint8_t*)v.c_str();
        nv.valuelen = v.size();
        nv.flags = NGHTTP3_NV_FLAG_NONE;
        stream_ctx->nvs.push_back(nv);
    }
    printf("[http3] On Recv Header\n");
    return 0;
}

// 헤더 섹션 종료 시 호출
int ServerHttp3::OnEndHeaders(nghttp3_conn *conn, int64_t stream_id, int fin,
                        void *conn_user_data, void *stream_user_data)
{
    printf("[http3] On End Headers\n");
    return 0;
}

int ServerHttp3::OnRecvData(nghttp3_conn *conn, int64_t stream_id, const uint8_t *data,
                      size_t len, void *conn_user_data, void *stream_user_data)
{
    // 바디 조각 수신. 애플리케이션이 실제로 처리 완료한 만큼
    // QUIC 플로우컨트롤 크레딧을 복구해야 함(예: MsQuic StreamReceiveComplete).
    StreamCtx *stream_ctx = (StreamCtx *)stream_user_data;

    auto &body = stream_ctx->request["body"];
    if (body.is_null())
        body = std::string();
    auto &s = body.get_ref<std::string &>();
    s.append(reinterpret_cast<const char *>(data), len);

    stream_ctx->ms_quic->StreamReceiveComplete(stream_ctx->stream, (uint64_t)len);

    printf("[http3] On Recv Data\n");
    return 0;
}

int ServerHttp3::OnDeferredConsume(nghttp3_conn *conn, int64_t stream_id, size_t consumed,
                             void *conn_user_data, void *stream_user_data)
{
    // 동기화 지연으로 지금 ‘소비’된 바이트 공지.
    // Consumed 만큼 추가로 크레딧 복구.
    printf("[http3] On Deffered Consume\n");
    return 0;
}

int ServerHttp3::OnEndStream(nghttp3_conn *conn, int64_t stream_id,
                       void *conn_user_data, void *stream_user_data)
{
    // 수신 측 스트림이 모두 끝남(서버=요청 완수, 클라=응답 완수)
    printf("[http3] On End Stream\n");
    return 0;
}

int ServerHttp3::OnStreamClose(nghttp3_conn *conn, int64_t stream_id, uint64_t app_err,
                         void *conn_user_data, void *stream_user_data)
{
    // 스트림 종료(정상/리셋 등). 정리 작업.
    printf("[http3] On Strean Close\n");
    return 0;
}

int ServerHttp3::OnAckedStreamData(nghttp3_conn *conn, int64_t stream_id, uint64_t data_len,
                             void *conn_user_data, void *stream_user_data)
{
    // 내가 보낸 바디 중 n바이트가 원격에서 ACK 됨 (버퍼 해제 타이밍 등)
    printf("[http3] On Acked Stream Data\n");
    return 0;
}

int ServerHttp3::OnStopSending(nghttp3_conn *conn, int64_t stream_id, uint64_t app_err,
                         void *conn_user_data, void *stream_user_data)
{
    // 라이브러리가 이 스트림에 STOP_SENDING 보내달라고 요청
    printf("[http3] On Stop Sending\n");
    return 0;
}

int ServerHttp3::OnResetStream(nghttp3_conn *conn, int64_t stream_id, uint64_t app_err,
                         void *conn_user_data, void *stream_user_data)
{
    // 라이브러리가 이 스트림을 RESET_STREAM 하라고 요청
    printf("[http3] On Reset Stream\n");
    return 0;
}

void ServerHttp3::OnRand(uint8_t *dest, size_t dest_len)
{
    printf("[http3] On Rand\n");
}

nghttp3_ssize ServerHttp3::ReadData(nghttp3_conn *conn, int64_t stream_id, nghttp3_vec *vec, size_t vec_cnt,
                              uint32_t *pflags, void *conn_user_data, void *stream_user_data)
{
    printf("[http3] Read Data Callback\n");
    *pflags = NGHTTP3_DATA_FLAG_EOF;
    return 0;
}

void ServerHttp3::SetContext(int64_t stream_id, StreamCtx *stream_ctx)
{
    if(int status = nghttp3_conn_set_stream_user_data(http3_connection, stream_id, stream_ctx)) {
        if(status == NGHTTP3_ERR_STREAM_NOT_FOUND) 
            printf("[http3] NGHTTP3_ERR_STREAM_NOT_FOUND\n");
        else 
            printf("[http3] nghttp3_conn_set_stream_user_data failed\n");
    }
}

long int ServerHttp3::Read(int64_t stream_id, QUIC_STREAM_EVENT *event)
{
    long int total_bytes = 0;

    for(uint32_t i = 0; i < event->RECEIVE.BufferCount; ++i) {
        const uint8_t* buf = event->RECEIVE.Buffers[i].Buffer;
        const int fin = ((event->RECEIVE.Flags & QUIC_RECEIVE_FLAG_FIN) && (i + 1 == event->RECEIVE.BufferCount)) ? 1 : 0;
        
        nghttp3_ssize n = nghttp3_conn_read_stream(http3_connection, stream_id, buf, event->RECEIVE.Buffers[i].Length, fin);
        if(n < 0) {
            fprintf(stderr, "nghttp3 read err=%zd (%s)\n", n, nghttp3_strerror((int)n));
            return n;
        }
        total_bytes += n;
    }
    printf("[http3] total read bytes : %ld\n", total_bytes);
    return total_bytes;
}

bool ServerHttp3::SetControlStreamId(int64_t control_stream_id)
{
    if(nghttp3_conn_bind_control_stream(http3_connection, control_stream_id)) {
        printf("[http3] control stream bind fail\n");
        return false;
    }
    this->control_stream_id = control_stream_id;
    return true;
}

bool ServerHttp3::SetQPACKStreamId(int64_t qpack_enc_stream_id, int64_t qpack_dec_stream_id)
{
    if(nghttp3_conn_bind_qpack_streams(http3_connection, qpack_enc_stream_id, qpack_dec_stream_id)) {
        printf("[http3] QPACK stream bind fail\n");
        return false;
    }
    this->qpack_enc_stream_id = qpack_enc_stream_id;
    this->qpack_dec_stream_id = qpack_dec_stream_id;
    return true;
}
