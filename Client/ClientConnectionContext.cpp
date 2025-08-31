#include "ClientConnectionContext.h"

/// @brief Connection의 Stream 객체들을 관리하는 클래스. 생성자에서는 상수값을 설정한다. 후에 연결이 완료되면 ConnectionContextInit()을 호출해야한다.
/// @param ms_quic 
/// @param quic_connection 
ClientConnectionContext::ClientConnectionContext(_In_ const QUIC_API_TABLE* ms_quic, _In_ HQUIC quic_registration, _In_ HQUIC quic_configuration, _In_z_ const char* target_domain, _In_range_(1, 65535) uint16_t target_port)
: ms_quic(ms_quic)
, target_domain(target_domain)
, target_port(target_port)
, quic_connection(GetConnection(quic_registration, quic_configuration, target_domain, target_port))
{
    if(quic_connection == nullptr) {
        return;
    }
    stream_map = new StreamMap(ms_quic);
}

ClientConnectionContext::~ClientConnectionContext()
{
    delete stream_map;
    delete http3;
}

/// @brief Connection Complete 이벤트에서 호출 http3에서 필요한 변수들을 할당한다.
/// @return 
bool ClientConnectionContext::Http3Init()
{
    if(!IsConnectionOk()) {
        printf("[http3] Connection is not available\n");
        return false;
    }

    if(
           !OpenStream(StreamTypes::UNIDIRECTION, http3_control_stream_id)
        || !OpenStream(StreamTypes::UNIDIRECTION, http3_qpack_encoder_stream_id)
        || !OpenStream(StreamTypes::UNIDIRECTION, http3_qpack_decoder_stream_id)) 
    {
        http3_stream_ok = false;
        printf("[http3] http3 stream open fail.\n");
    }
    else
    {
        http3 = new ClientHttp3(this, http3_control_stream_id, http3_qpack_encoder_stream_id, http3_qpack_decoder_stream_id, target_domain, target_port);
        http3_stream_ok = true;
    } 
    return true;
}

HQUIC ClientConnectionContext::GetConnection(_In_ HQUIC quic_registration, _In_ HQUIC quic_configuration, _In_z_ const char *target, _In_range_(1, 65535) uint16_t port)
{
    QUIC_STATUS status;
    const char* resumption_ticket_string = NULL;
    const char* ssl_key_log_file = getenv(ssl_key_log_env_var);
    HQUIC connection = NULL;

    //
    // Allocate a new connection object.
    //
    if (QUIC_FAILED(status = ms_quic->ConnectionOpen(quic_registration, ClientConnectionCallback, this, &connection))) {
        printf("[conn] ConnectionOpen failed, 0x%x!\n", status);
        goto Error;
    }

    // if ((ResumptionTicketString = GetValue(argc, argv, "ticket")) != NULL) {
    //     //
    //     // If provided at the command line, set the resumption ticket that can
    //     // be used to resume a previous session.
    //     //
    //     uint8_t ResumptionTicket[10240];
    //     uint16_t TicketLength = (uint16_t)DecodeHexBuffer(ResumptionTicketString, sizeof(ResumptionTicket), ResumptionTicket);
    //     if (QUIC_FAILED(Status = MsQuic->SetParam(Connection, QUIC_PARAM_CONN_RESUMPTION_TICKET, TicketLength, ResumptionTicket))) {
    //         printf("SetParam(QUIC_PARAM_CONN_RESUMPTION_TICKET) failed, 0x%x!\n", Status);
    //         goto Error;
    //     }
    // }

    if (ssl_key_log_file != NULL) {
        printf("[ssl] SSL KEY LOG FILE FOUND\n");
        if (QUIC_FAILED(status = ms_quic->SetParam(connection, QUIC_PARAM_CONN_TLS_SECRETS, sizeof(client_secrets), &client_secrets))) {
            printf("[ssl] SetParam(QUIC_PARAM_CONN_TLS_SECRETS) failed, 0x%x!\n", status);
            goto Error;
        }
    }

    printf("[conn][%p] Connecting...\n", connection);

    //
    // Start the connection to the server.
    //
    if (QUIC_FAILED(status = ms_quic->ConnectionStart(connection, quic_configuration, QUIC_ADDRESS_FAMILY_UNSPEC, target, port))) {
        printf("[conn] ConnectionStart failed, 0x%x!\n", status);
        goto Error;
    }

    Error:

        if (QUIC_FAILED(status) && connection != NULL) {
            ms_quic->ConnectionClose(connection);
            return nullptr;
        }

    return connection;
}

/// @brief 스트림 생성 후 반환
/// @param stream_type 양방향, 단방향 여부 
/// @return 스트림 객체를 반환한다. 실패한 경우 nullptr 반환
HQUIC ClientConnectionContext::GetStream(StreamTypes stream_type)
{
    QUIC_STATUS status;
    HQUIC stream = NULL;
    
    if (QUIC_FAILED(status = ms_quic->StreamOpen(quic_connection, stream_type == StreamTypes::BIDIRECTION ? QUIC_STREAM_OPEN_FLAG_NONE : QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL, ClientStreamCallback, this, &stream))) {
        printf("[strm] StreamOpen failed, 0x%x!\n", status);
        goto Error;
    }

    printf("[strm][%p] %s Starting...\n", stream, stream_type == StreamTypes::BIDIRECTION ? "Bidirection Stream" : "Unidirection Stream");

    if (QUIC_FAILED(status = ms_quic->StreamStart(stream, QUIC_STREAM_START_FLAG_NONE))) {
        printf("[strm] StreamStart failed, 0x%x!\n", status);
        ms_quic->StreamClose(stream);
        goto Error;
    }

    Error:
        if (QUIC_FAILED(status)) {
            ms_quic->ConnectionShutdown(quic_connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
            return nullptr;
        }
    return stream;
}

/// @brief 스트림을 열고 Map에 등록 후 스트림 생성 여부 반환
/// @param stream_type 단방향, 양방향 여부 결정
/// @param stream_id 스트림 아이디를 받을 포인터
/// @return 스트림 생성 여부 반환
bool ClientConnectionContext::OpenStream(StreamTypes stream_type, uint64_t& stream_id)
{
    HQUIC stream = GetStream(stream_type);
    if(stream == nullptr) return false;

    stream_id = GetStreamId(stream);
    StreamElement element(stream, StreamStatus::Idle);

    stream_map->InsertStream(stream_id, element);
    return true;
}

/// @brief Datagram 전송.
/// 데이터 유실 가능성이 있는 보안 전송
/// 네트워크 상황에 따른 max_send_length 값으로 나누어 전송하며 도착 순서를 알 수 없다.
/// @param data 전송할 데이터
/// @param len data의 길이
void ClientConnectionContext::DatagramSend(uint8_t *data, size_t len)
{
    if(!IsConnectionOk() || !datagram_send_enabled) {
        printf("[dagr][%p] Connection Unavailable\n", quic_connection);
        return;
    }

    QUIC_STATUS status;
    printf("[dagr][%p] Sending data...\n", quic_connection);
    // Datagram 전송의 경우 QUIC의 암호화 기능을 제외하고 모두 사용하지 못한다. 
    // StreamSend에서는 주어진 버퍼의 크기가 매우 커도 QUIC에서 알아서 나누어 전송하는 반면
    // DatagramSend에서는 MaxSendLength (MTU)의 크기 이상을 전송할 수 없다.
    // 때문에 애플리케이션에서 알아서 나누어 전송해야한다.
    
    // DTLS와도 유사하다고 보이는데 차이점은 다음과 같다.
    // 1. 계층이 다르다. QUIC - 전송 계층, DTLS - UDP 위에서 동작하는 보안 프로토콜
    // 2. QUIC는 손실탐지 및 혼잡제어를 제공한다.
    while(len) {
        uint32_t chunk = (uint16_t)(std::min)(len, (size_t)max_send_length);
        // QUIC_BUFFER 구조체의 크기 (포인터 + 길이 값) + 데이터 길이 만큼 할당한다.
        uint8_t* send_buffer_raw = (uint8_t*)malloc(sizeof(QUIC_BUFFER) + chunk);
        if (send_buffer_raw == NULL) {
            printf("[dagr][%p] SendBuffer allocation failed!\n", quic_connection);
            status = QUIC_STATUS_OUT_OF_MEMORY;
            return;
        }
        QUIC_BUFFER* send_buffer = (QUIC_BUFFER*)send_buffer_raw;
        // SendBufferRaw에는 QUIC_BUFFER 구조체 데이터가 포함되어있으므로
        // QUIC_BUFFER 만큼 더해서 포인터를 실제 데이터의 시작 부분을 가리키도록 이동시킨다. 
        send_buffer->Buffer = send_buffer_raw + sizeof(QUIC_BUFFER);
        // 구조체를 제외한 데이터의 길이
        send_buffer->Length = chunk;
        memcpy(send_buffer->Buffer, data, chunk);
        
        if(QUIC_FAILED(status = ms_quic->DatagramSend(quic_connection, send_buffer, 1, QUIC_SEND_FLAG_NONE, send_buffer))) {
            printf("[dagr] DatagramSend failed, 0x%x!\n", status);
            free(send_buffer_raw);
        }

        len -= chunk;
        data += chunk;
    }
}

void ClientConnectionContext::StreamSendById(int64_t stream_id, uint8_t *data, size_t len, bool fin)
{
    StreamSend(stream_map->GetStreamById(stream_id)->stream, data, len, fin);
}

void ClientConnectionContext::StreamSendById(int64_t stream_id, QUIC_BUFFER *buffers, size_t buffers_len, bool fin)
{
    StreamSend(stream_map->GetStreamById(stream_id)->stream, buffers, buffers_len, fin);
}

void ClientConnectionContext::StreamSend(_In_ HQUIC stream, uint8_t* data, size_t len, bool fin)
{
    if(!IsConnectionOk()) {
        printf("[strm] Connection Unavailable\n");
        return;
    }
    uint8_t* send_buffer_raw;
    QUIC_BUFFER* send_buffer;
    QUIC_STATUS status;

    //
    // Allocates and builds the buffer to send over the stream.
    //
    send_buffer_raw = (uint8_t*)malloc(sizeof(QUIC_BUFFER) + len);
    if (send_buffer_raw == NULL) {
        printf("[strm] SendBuffer allocation failed!\n");
        status = QUIC_STATUS_OUT_OF_MEMORY;
        return;
    }
    send_buffer = (QUIC_BUFFER*)send_buffer_raw;
    send_buffer->Buffer = send_buffer_raw + sizeof(QUIC_BUFFER);
    send_buffer->Length = static_cast<uint32_t>(len);
    std::memcpy(send_buffer->Buffer, data, len);

    printf("[strm][%p] Sending data...\n", stream);

    //
    // Sends the buffer over the stream. Note the FIN flag is passed along with
    // the buffer. This indicates this is the last buffer on the stream and the
    // the stream is shut down (in the send direction) immediately after.
    //
    if (QUIC_FAILED(status = ms_quic->StreamSend(stream, send_buffer, 1, fin ? QUIC_SEND_FLAG_FIN : QUIC_SEND_FLAG_NONE, send_buffer))) {
        printf("[strm] StreamSend failed, 0x%x!\n", status);
        free(send_buffer_raw);
    }
}

void ClientConnectionContext::StreamSend(HQUIC stream, QUIC_BUFFER *send_buffer, size_t buffer_len, bool fin)
{
    if(!IsConnectionOk()) {
        printf("[strm] Connection Unavailable\n");
        return;
    }
    QUIC_STATUS status;
    printf("[strm][%p] Sending data...\n", stream);

    //
    // Sends the buffer over the stream. Note the FIN flag is passed along with
    // the buffer. This indicates this is the last buffer on the stream and the
    // the stream is shut down (in the send direction) immediately after.
    //
    if (QUIC_FAILED(status = ms_quic->StreamSend(stream, send_buffer, 1, fin ? QUIC_SEND_FLAG_FIN : QUIC_SEND_FLAG_NONE, send_buffer))) {
        printf("[strm] StreamSend failed, 0x%x!\n", status);
        free(send_buffer);
    }
}

void ClientConnectionContext::ConnectionShutdown(bool send_notify, QUIC_UINT62 error_code)
{
    if(!IsConnectionOk()) {
        printf("[conn] Connection Already Down\n");
        return;
    }
    printf("[conn] Shutdown Connection\n");
    ms_quic->ConnectionShutdown(quic_connection, send_notify ? QUIC_CONNECTION_SHUTDOWN_FLAG_NONE : QUIC_CONNECTION_SHUTDOWN_FLAG_SILENT, error_code);
}


_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS
QUIC_API
ClientConnectionContext::ClientConnectionCallback(
    _In_ HQUIC connection,
    _In_opt_ void* context,
    _Inout_ QUIC_CONNECTION_EVENT* event
    )
{
    ClientConnectionContext* this_context = (ClientConnectionContext*)context;

    if (event->Type == QUIC_CONNECTION_EVENT_CONNECTED) {
        const char* ssl_key_log_file = getenv(ssl_key_log_env_var);
        if (ssl_key_log_file != NULL) {
            this_context->WriteSslKeyLogFile(ssl_key_log_file, &this_context->client_secrets);
        }
    }

    bool send_enabled;

    switch (event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        //
        // The handshake has completed for the connection.
        //
        printf("[conn][%p] Connected\n", connection);
        this_context->connection_ok = true;
        // ClientSend(Connection);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        //
        // The connection has been shut down by the transport. Generally, this
        // is the expected way for the connection to shut down with this
        // protocol, since we let idle timeout kill the connection.
        //
        if (event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status == QUIC_STATUS_CONNECTION_IDLE) {
            printf("[conn][%p] Successfully shut down on idle.\n", connection);
        } else {
            printf("[conn][%p] Shut down by transport, 0x%x\n", connection, event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
        }
        this_context->connection_ok = false;
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        //
        // The connection was explicitly shut down by the peer.
        //
        printf("[conn][%p] Shut down by peer, 0x%llu\n", connection, (unsigned long long)event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
        this_context->connection_ok = false;
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        //
        // The connection has completed the shutdown process and is ready to be
        // safely cleaned up.
        //
        printf("[conn][%p] All done\n", connection);
        if (!event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
            this_context->ms_quic->ConnectionClose(connection);
        }
        this_context->connection_ok = false;
        break;
    case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED:
        //
        // A resumption ticket (also called New Session Ticket or NST) was
        // received from the server.
        //
        printf("[conn][%p] Resumption ticket received (%u bytes):\n", connection, event->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength);
        for (uint32_t i = 0; i < event->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength; i++) {
            printf("%.2X", (uint8_t)event->RESUMPTION_TICKET_RECEIVED.ResumptionTicket[i]);
        }
        printf("\n");
        break;
    case QUIC_CONNECTION_EVENT_IDEAL_PROCESSOR_CHANGED:
        printf(
            "[dagr][%p] Ideal Processor is: %u, Partition Index %u\n",
            connection,
            event->IDEAL_PROCESSOR_CHANGED.IdealProcessor,
            event->IDEAL_PROCESSOR_CHANGED.PartitionIndex);
        break;
    case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED:
        this_context->max_send_length = event->DATAGRAM_STATE_CHANGED.MaxSendLength;
        send_enabled = event->DATAGRAM_STATE_CHANGED.SendEnabled;
        printf("[dagr] Datagram MaxSendLength : %d\n", this_context->max_send_length);
        
        if(send_enabled != this_context->datagram_send_enabled) {
            this_context->datagram_send_enabled = send_enabled;
            printf("[dagr] SendEnabled Changed : %s\n", this_context->datagram_send_enabled ? "true" : "false");
        }
        break;
    case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED:
        if(QUIC_DATAGRAM_SEND_STATE_IS_FINAL(event->DATAGRAM_SEND_STATE_CHANGED.State)) {
            free(event->DATAGRAM_SEND_STATE_CHANGED.ClientContext);
            printf("[dagr] Send Complete\n");
        }

        break;
    case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED:
        printf("[dagr] Data Received (%d bytes) : %.*s\n", (int)event->DATAGRAM_RECEIVED.Buffer->Length, (int)event->DATAGRAM_RECEIVED.Buffer->Length, (const char*)event->DATAGRAM_RECEIVED.Buffer->Buffer);
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS
QUIC_API
ClientConnectionContext::ClientStreamCallback(
    _In_ HQUIC stream,
    _In_opt_ void* context,
    _Inout_ QUIC_STREAM_EVENT* event
    )
{
    ClientConnectionContext* this_context = (ClientConnectionContext*)context;

    // StreamElement* Element = StreamMap->GetStreamByHQUIC(Stream);
    switch (event->Type) {
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        //
        // A previous StreamSend call has completed, and the context is being
        // returned back to the app.
        //
        free(event->SEND_COMPLETE.ClientContext);
        printf("[strm][%p] Data sent\n", stream);
        break;
    case QUIC_STREAM_EVENT_RECEIVE:
        //
        // Data was received from the peer on the stream.
        //
        printf("[strm][%p] Data received. Buffer Count : %d\n", stream, event->RECEIVE.BufferCount);
        if(event->RECEIVE.BufferCount > 0) {
            printf("Len : %d\n", event->RECEIVE.Buffers[0].Length);
            for(unsigned int i = 0; i < event->RECEIVE.Buffers[0].Length; ++i) {
                printf("%d", event->RECEIVE.Buffers[0].Buffer[i]);
            }
            printf("\n");
        }
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        //
        // The peer gracefully shut down its send direction of the stream.
        //
        printf("[strm][%p] Peer aborted\n", stream);
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        //
        // The peer aborted its send direction of the stream.
        //
        printf("[strm][%p] Peer shut down\n", stream);
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        //
        // Both directions of the stream have been shut down and MsQuic is done
        // with the stream. It can now be safely cleaned up.
        //
        printf("[strm][%p] All done\n", stream);
        if (!event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
            // StreamMap->EraseStreamByHQUIC(Stream);
            this_context->ms_quic->StreamClose(stream);
        }
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

void
ClientConnectionContext::EncodeHexBuffer(
    _In_reads_(buffer_len) uint8_t* buffer,
    _In_ uint8_t buffer_len,
    _Out_writes_bytes_(2*buffer_len) char* hex_string
    )
{
    #define HEX_TO_CHAR(x) ((x) > 9 ? ('a' + ((x) - 10)) : '0' + (x))
    for (uint8_t i = 0; i < buffer_len; i++) {
        hex_string[i*2]     = HEX_TO_CHAR(buffer[i] >> 4);
        hex_string[i*2 + 1] = HEX_TO_CHAR(buffer[i] & 0xf);
    }
}

void
ClientConnectionContext::WriteSslKeyLogFile(
    _In_z_ const char* file_name,
    _In_ QUIC_TLS_SECRETS* tls_secrets
    )
{
    printf("[ssl] Writing SSLKEYLOGFILE at %s\n", file_name);
    FILE* file = NULL;
#ifdef _WIN32
    file = _fsopen(file_name, "wb", _SH_DENYNO);
#else
    file = fopen(FileName, "wb");
#endif

    if (file == NULL) {
        printf("[ssl] Failed to open sslkeylogfile %s\n", file_name);
        return;
    }
    // if (fseek(File, 0, SEEK_END) == 0 && ftell(File) == 0) {
        fprintf(file, "# TLS 1.3 secrets log file, generated by msquic\n");
    // }
    

    char client_random_buffer[(2 * sizeof(((QUIC_TLS_SECRETS*)0)->ClientRandom)) + 1] = {0};

    char temp_hex_buffer[(2 * QUIC_TLS_SECRETS_MAX_SECRET_LEN) + 1] = {0};
    if (tls_secrets->IsSet.ClientRandom) {
        EncodeHexBuffer(
            tls_secrets->ClientRandom,
            (uint8_t)sizeof(tls_secrets->ClientRandom),
            client_random_buffer);
    }

    if (tls_secrets->IsSet.ClientEarlyTrafficSecret) {
        EncodeHexBuffer(
            tls_secrets->ClientEarlyTrafficSecret,
            tls_secrets->SecretLength,
            temp_hex_buffer);
        fprintf(
            file,
            "CLIENT_EARLY_TRAFFIC_SECRET %s %s\n",
            client_random_buffer,
            temp_hex_buffer);
    }

    if (tls_secrets->IsSet.ClientHandshakeTrafficSecret) {
        EncodeHexBuffer(
            tls_secrets->ClientHandshakeTrafficSecret,
            tls_secrets->SecretLength,
            temp_hex_buffer);
        fprintf(
            file,
            "CLIENT_HANDSHAKE_TRAFFIC_SECRET %s %s\n",
            client_random_buffer,
            temp_hex_buffer);
    }

    if (tls_secrets->IsSet.ServerHandshakeTrafficSecret) {
        EncodeHexBuffer(
            tls_secrets->ServerHandshakeTrafficSecret,
            tls_secrets->SecretLength,
            temp_hex_buffer);
        fprintf(
            file,
            "SERVER_HANDSHAKE_TRAFFIC_SECRET %s %s\n",
            client_random_buffer,
            temp_hex_buffer);
    }

    if (tls_secrets->IsSet.ClientTrafficSecret0) {
        EncodeHexBuffer(
            tls_secrets->ClientTrafficSecret0,
            tls_secrets->SecretLength,
            temp_hex_buffer);
        fprintf(
            file,
            "CLIENT_TRAFFIC_SECRET_0 %s %s\n",
            client_random_buffer,
            temp_hex_buffer);
    }

    if (tls_secrets->IsSet.ServerTrafficSecret0) {
        EncodeHexBuffer(
            tls_secrets->ServerTrafficSecret0,
            tls_secrets->SecretLength,
            temp_hex_buffer);
        fprintf(
            file,
            "SERVER_TRAFFIC_SECRET_0 %s %s\n",
            client_random_buffer,
            temp_hex_buffer);
    }

    fflush(file);
    fclose(file);
}