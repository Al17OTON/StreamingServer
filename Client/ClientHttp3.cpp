#include "ClientHttp3.h"
// 순환 include를 방지하기 위해 .cpp에서 헤더 선언
// #pragma once에 의해 다른 한 헤더파일에서는 빈 헤더파일을 참조하게된다.
#include "ClientConnectionContext.h"

ClientHttp3::ClientHttp3(_In_ void* client_connection_context, int64_t control_stream_id, int64_t qpack_enc_stream_id, int64_t qpack_dec_stream_id, _In_z_ const char* target_doamin, uint16_t target_port)
: control_stream_id(control_stream_id)
, qpack_enc_stream_id(qpack_enc_stream_id)
, qpack_dec_stream_id(qpack_dec_stream_id)
, target_domain(target_domain)
, target_port(target_port)
, authority((std::string(target_doamin) + ":" + std::to_string(target_port)).c_str())
{
    
    nghttp3_settings settings;
    nghttp3_callbacks callbacks{};

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

    if(nghttp3_conn_client_new(&http3_connection, &callbacks, &settings, NULL, client_connection_context)) {
        printf("[http3] Nghttp3 client new fail\n");
        return;
    }
    nghttp3_conn_bind_control_stream(http3_connection, control_stream_id);
    nghttp3_conn_bind_qpack_streams(http3_connection, qpack_enc_stream_id, qpack_dec_stream_id);
    printf("[http3] Nghttp3 client Start\n");
}

ClientHttp3::~ClientHttp3()
{
    
}

void ClientHttp3::Send(int64_t stream_id, void* quic_connection)
{
    ClientConnectionContext* client_connection_context = (ClientConnectionContext*)quic_connection;
    nghttp3_data_reader data_reader = {ReadData};
    nghttp3_nv name_value_arr[] = {
        { (uint8_t*)":method", (uint8_t*)"GET", 7, 3, NGHTTP3_NV_FLAG_NONE },
        { (uint8_t*)":scheme", (uint8_t*)"https", 7, 5, NGHTTP3_NV_FLAG_NONE },
        { (uint8_t*)":authority", (uint8_t*)authority, 10, (size_t)strlen(authority), NGHTTP3_NV_FLAG_NONE },
        { (uint8_t*)":path", (uint8_t*)"/", 5, 1, NGHTTP3_NV_FLAG_NONE },
        // 필요 시 일반 헤더 추가
        // { (uint8_t*)"user-agent",10,(uint8_t*)"h3-test",7, NGHTTP3_NV_FLAG_NONE },
    };
    
    if(nghttp3_conn_submit_request(http3_connection, stream_id, name_value_arr, 4, &data_reader, nullptr)) {
        printf("[http3] submit request failed\n");
        return;
    }

    // 이미 누가 전송중인 경우 리턴
    if(pumping.test_and_set(std::memory_order_acquire)) return;

    printf("[http3] Sending....\n");
    while(true) {
        int64_t out_stream_id;
        int fin; 
        nghttp3_vec vecs[16];
        nghttp3_ssize size = nghttp3_conn_writev_stream(http3_connection, &out_stream_id, &fin, vecs, 16);
        
        if(size <= 0 && out_stream_id < 0) break;

        QUIC_BUFFER_CONVERT buffers = NgVec2QuicBuffer(vecs, size);

        client_connection_context->StreamSendById(out_stream_id, buffers.buffers, size, fin ? true : false);

        nghttp3_conn_add_write_offset(http3_connection, out_stream_id, buffers.bytes);
    }

    pumping.clear(std::memory_order_release);
}
