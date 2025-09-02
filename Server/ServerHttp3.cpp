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
