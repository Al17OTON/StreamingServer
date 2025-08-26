#include "QUICClient.h"

const QUIC_API_TABLE*   QUICClient::MsQuic      = nullptr;
HQUIC                   QUICClient::Registration = nullptr;
HQUIC                   QUICClient::Configuration = nullptr;
bool                    QUICClient::Ok = false;    
bool                    QUICClient::ConnectionOk = false;
bool                    QUICClient::DatagramSendEnabled = false;      
uint32_t                QUICClient::MaxSendLength = 0;  
QUIC_TLS_SECRETS        QUICClient::ClientSecrets{};

bool QUICClient::SetConfiguration(bool Secure)
{
    // https://github.com/microsoft/msquic/blob/main/docs/api/QUIC_SETTINGS.md
    // QUIC 설정 구조체
    QUIC_SETTINGS Settings = {0};
    //
    // Configures the client's idle timeout.
    //
    Settings.IdleTimeoutMs = IdleTimeoutMs;
    Settings.IsSet.IdleTimeoutMs = TRUE;

    // Datagram 활성화
    Settings.DatagramReceiveEnabled = TRUE;
    Settings.IsSet.DatagramReceiveEnabled = TRUE; 

    //
    // Configures a default client configuration, optionally disabling
    // server certificate validation.
    //
    QUIC_CREDENTIAL_CONFIG CredConfig;
    memset(&CredConfig, 0, sizeof(CredConfig));
    CredConfig.Type = QUIC_CREDENTIAL_TYPE_NONE;
    CredConfig.Flags = QUIC_CREDENTIAL_FLAG_CLIENT;
    
    if(!Secure) {
        CredConfig.Flags |= QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
        printf("[config] -------CA Verification disabled.-------\n");
    }
    else {
        // QUIC_CREDENTIAL_FLAG_USE_TLS_BUILTIN_CERTIFICATE_VALIDATION - OpenSSL의 기본 검증을 사용 (SCHANNEL의 경우 무용지물)
        // QUIC_CREDENTIAL_FLAG_SET_CA_CERTIFICATE_FILE - CA 파일을 사용하겠다고 명시
        // 윈도우에서 OpenSSL 사용시 모두 활성화 해줘야한다.
        CredConfig.Flags |= QUIC_CREDENTIAL_FLAG_USE_TLS_BUILTIN_CERTIFICATE_VALIDATION
                            | QUIC_CREDENTIAL_FLAG_SET_CA_CERTIFICATE_FILE;
                            
        // CA 파일 지정 (윈도우의 경우)
        CredConfig.CaCertificateFile = (char*)CAFile;
        printf("[config] -------CA Verification enabled-------\n");
    }

    //
    // Allocate/initialize the configuration object, with the configured ALPN
    // and settings.
    //
    if (QUIC_FAILED(MsQuic->ConfigurationOpen(Registration, Alpn, 2, &Settings, sizeof(Settings), NULL, &Configuration))) {
        printf("[config] ConfigurationOpen failed!\n");
        return FALSE;
    }

    //
    // Loads the TLS credential part of the configuration. This is required even
    // on client side, to indicate if a certificate is required or not.
    //
    if (QUIC_FAILED(MsQuic->ConfigurationLoadCredential(Configuration, &CredConfig))) {
        printf("[config] ConfigurationLoadCredential failed!\n");
        return FALSE;
    }

    return TRUE;
}

QUICClient::QUICClient(bool Secure)
{
    // https://github.com/nlohmann/json?tab=readme-ov-file
    // Json CPP
    printf("[init] Using Json library - %s\n", JSON::meta().dump().c_str());

    if(MsQuic) {
        printf("[init] Client is Already on.\n");
        return;
    }

    // VideoEncoder vid("/home/ubuntu/Sample/sample.mp4");
    // MSQUIC API 테이블을 생성한다. 버전은 1과 2가 있는데 1은 사용 중지되었다.
    // 생성이 잘 되었는지 unsigned int를 반환하는데 이것을 QUIC_FAILED, QUIC_SUCCEEDED에서 확인할 수 있다.
    if(QUIC_FAILED(MsQuicOpen2(&MsQuic))) {
        printf("[init] QUIC Open Fail\n");
        return;
    }

    if(QUIC_FAILED(MsQuic->RegistrationOpen(&RegConfig, &Registration))) {
        printf("[init] RegistrationOpen Fail\n");
        return;
    }
    // 서버와 연결시 인증서를 검증할 것이라면 true, 그렇지 않다면 false
    // 검증하지 않아도 통신 자체는 암호화 되지만 MITM(Man-in-the-Middle) 공격에 취약해진다.
    if(!SetConfiguration(Secure)) {
        return;
    }

    // printf("Server Started and Listening on Port : %d\n", UdpPort);
    printf("[init] Client Init Success\n");
    Ok = true;
    ConnectionOk = false;
}


QUICClient::~QUICClient()
{
    if(Configuration) MsQuic->ConfigurationClose(Configuration);

    if(Registration) MsQuic->RegistrationClose(Registration);

    if(MsQuic) MsQuicClose(MsQuic);

    Ok = false;
    ConnectionOk = false;
    printf("[end] Client Closed\n");
}

HQUIC QUICClient::GetStream(HQUIC Connection, StreamTypes StreamType)
{
    QUIC_STATUS Status;
    HQUIC Stream = NULL;

    //
    // Create/allocate a new bidirectional stream. The stream is just allocated
    // and no QUIC stream identifier is assigned until it's started.
    //
    if (QUIC_FAILED(Status = MsQuic->StreamOpen(Connection, StreamType == BIDIRECTION ? QUIC_STREAM_OPEN_FLAG_NONE : QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL, ClientStreamCallback, NULL, &Stream))) {
        printf("[strm] StreamOpen failed, 0x%x!\n", Status);
        goto Error;
    }

    printf("[strm][%p] %s Starting...\n", Stream, StreamType == BIDIRECTION ? "Bidirection Stream" : "Unidirection Stream");

    //
    // Starts the bidirectional stream. By default, the peer is not notified of
    // the stream being started until data is sent on the stream.
    //
    if (QUIC_FAILED(Status = MsQuic->StreamStart(Stream, QUIC_STREAM_START_FLAG_NONE))) {
        printf("[strm] StreamStart failed, 0x%x!\n", Status);
        MsQuic->StreamClose(Stream);
        goto Error;
    }

    Error:

        if (QUIC_FAILED(Status)) {
            MsQuic->ConnectionShutdown(Connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
            return nullptr;
        }
    return Stream;
}

HQUIC QUICClient::GetConnection(_In_z_ const char *Target, _In_range_(1, 65535) uint16_t Port)
{
    QUIC_STATUS Status;
    const char* ResumptionTicketString = NULL;
    const char* SslKeyLogFile = getenv(SslKeyLogEnvVar);
    HQUIC Connection = NULL;

    //
    // Allocate a new connection object.
    //
    if (QUIC_FAILED(Status = MsQuic->ConnectionOpen(Registration, ClientConnectionCallback, NULL, &Connection))) {
        printf("[conn] ConnectionOpen failed, 0x%x!\n", Status);
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

    if (SslKeyLogFile != NULL) {
        printf("[ssl] SSL KEY LOG FILE FOUND\n");
        if (QUIC_FAILED(Status = MsQuic->SetParam(Connection, QUIC_PARAM_CONN_TLS_SECRETS, sizeof(ClientSecrets), &ClientSecrets))) {
            printf("[ssl] SetParam(QUIC_PARAM_CONN_TLS_SECRETS) failed, 0x%x!\n", Status);
            goto Error;
        }
    }

    printf("[conn][%p] Connecting...\n", Connection);

    //
    // Start the connection to the server.
    //
    if (QUIC_FAILED(Status = MsQuic->ConnectionStart(Connection, Configuration, QUIC_ADDRESS_FAMILY_UNSPEC, Target, Port))) {
        printf("[conn] ConnectionStart failed, 0x%x!\n", Status);
        goto Error;
    }

    Error:

        if (QUIC_FAILED(Status) && Connection != NULL) {
            MsQuic->ConnectionClose(Connection);
            return nullptr;
        }

    return Connection;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS
QUIC_API
QUICClient::ClientConnectionCallback(
    _In_ HQUIC Connection,
    _In_opt_ void* Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event
    )
{
    UNREFERENCED_PARAMETER(Context);

    if (Event->Type == QUIC_CONNECTION_EVENT_CONNECTED) {
        const char* SslKeyLogFile = getenv(SslKeyLogEnvVar);
        if (SslKeyLogFile != NULL) {
            WriteSslKeyLogFile(SslKeyLogFile, &ClientSecrets);
        }
    }

    bool SendEnabled;

    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        //
        // The handshake has completed for the connection.
        //
        printf("[conn][%p] Connected\n", Connection);
        ConnectionOk = true;
        // ClientSend(Connection);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        //
        // The connection has been shut down by the transport. Generally, this
        // is the expected way for the connection to shut down with this
        // protocol, since we let idle timeout kill the connection.
        //
        if (Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status == QUIC_STATUS_CONNECTION_IDLE) {
            printf("[conn][%p] Successfully shut down on idle.\n", Connection);
        } else {
            printf("[conn][%p] Shut down by transport, 0x%x\n", Connection, Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
        }
        ConnectionOk = false;
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        //
        // The connection was explicitly shut down by the peer.
        //
        printf("[conn][%p] Shut down by peer, 0x%llu\n", Connection, (unsigned long long)Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
        ConnectionOk = false;
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        //
        // The connection has completed the shutdown process and is ready to be
        // safely cleaned up.
        //
        printf("[conn][%p] All done\n", Connection);
        if (!Event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
            MsQuic->ConnectionClose(Connection);
        }
        ConnectionOk = false;
        break;
    case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED:
        //
        // A resumption ticket (also called New Session Ticket or NST) was
        // received from the server.
        //
        printf("[conn][%p] Resumption ticket received (%u bytes):\n", Connection, Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength);
        for (uint32_t i = 0; i < Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength; i++) {
            printf("%.2X", (uint8_t)Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicket[i]);
        }
        printf("\n");
        break;
    case QUIC_CONNECTION_EVENT_IDEAL_PROCESSOR_CHANGED:
        printf(
            "[dagr][%p] Ideal Processor is: %u, Partition Index %u\n",
            Connection,
            Event->IDEAL_PROCESSOR_CHANGED.IdealProcessor,
            Event->IDEAL_PROCESSOR_CHANGED.PartitionIndex);
        break;
    case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED:
        MaxSendLength = Event->DATAGRAM_STATE_CHANGED.MaxSendLength;
        SendEnabled = Event->DATAGRAM_STATE_CHANGED.SendEnabled;
        printf("[dagr] Datagram MaxSendLength : %d\n", MaxSendLength);
        
        if(SendEnabled != DatagramSendEnabled) {
            DatagramSendEnabled = SendEnabled;
            printf("[dagr] SendEnabled Changed : %s\n", DatagramSendEnabled ? "true" : "false");
        }
        break;
    case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED:
        if(QUIC_DATAGRAM_SEND_STATE_IS_FINAL(Event->DATAGRAM_SEND_STATE_CHANGED.State)) {
            free(Event->DATAGRAM_SEND_STATE_CHANGED.ClientContext);
            printf("[dagr] Send Complete\n");
        }

        break;
    case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED:
        printf("[dagr] Data Received (%d bytes) : %.*s\n", (int)Event->DATAGRAM_RECEIVED.Buffer->Length, (int)Event->DATAGRAM_RECEIVED.Buffer->Length, (const char*)Event->DATAGRAM_RECEIVED.Buffer->Buffer);
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
QUICClient::ClientStreamCallback(
    _In_ HQUIC Stream,
    _In_opt_ void* Context,
    _Inout_ QUIC_STREAM_EVENT* Event
    )
{
    UNREFERENCED_PARAMETER(Context);
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        //
        // A previous StreamSend call has completed, and the context is being
        // returned back to the app.
        //
        free(Event->SEND_COMPLETE.ClientContext);
        printf("[strm][%p] Data sent\n", Stream);
        break;
    case QUIC_STREAM_EVENT_RECEIVE:
        //
        // Data was received from the peer on the stream.
        //
        printf("[strm][%p] Data received. Buffer Count : %d\n", Stream, Event->RECEIVE.BufferCount);
        if(Event->RECEIVE.BufferCount > 0) {
            printf("Len : %d\n", Event->RECEIVE.Buffers[0].Length);
            for(int i = 0; i < Event->RECEIVE.Buffers[0].Length; ++i) {
                printf("%d", Event->RECEIVE.Buffers[0].Buffer[i]);
            }
            printf("\n");
        }
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        //
        // The peer gracefully shut down its send direction of the stream.
        //
        printf("[strm][%p] Peer aborted\n", Stream);
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        //
        // The peer aborted its send direction of the stream.
        //
        printf("[strm][%p] Peer shut down\n", Stream);
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        //
        // Both directions of the stream have been shut down and MsQuic is done
        // with the stream. It can now be safely cleaned up.
        //
        printf("[strm][%p] All done\n", Stream);
        if (!Event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
            MsQuic->StreamClose(Stream);
        }
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

void QUICClient::StreamSend(_In_ HQUIC Stream, uint8_t* Data, size_t Length, bool Fin)
{
    if(!IsConnectionOk()) {
        printf("[strm] Connection Unavailable\n");
        return;
    }
    uint8_t* SendBufferRaw;
    QUIC_BUFFER* SendBuffer;
    QUIC_STATUS Status;

    //
    // Allocates and builds the buffer to send over the stream.
    //
    SendBufferRaw = (uint8_t*)malloc(sizeof(QUIC_BUFFER) + Length);
    if (SendBufferRaw == NULL) {
        printf("[strm] SendBuffer allocation failed!\n");
        Status = QUIC_STATUS_OUT_OF_MEMORY;
        return;
    }
    SendBuffer = (QUIC_BUFFER*)SendBufferRaw;
    SendBuffer->Buffer = SendBufferRaw + sizeof(QUIC_BUFFER);
    SendBuffer->Length = static_cast<uint32_t>(Length);
    std::memcpy(SendBuffer->Buffer, Data, Length);

    printf("[strm][%p] Sending data...\n", Stream);

    //
    // Sends the buffer over the stream. Note the FIN flag is passed along with
    // the buffer. This indicates this is the last buffer on the stream and the
    // the stream is shut down (in the send direction) immediately after.
    //
    if (QUIC_FAILED(Status = MsQuic->StreamSend(Stream, SendBuffer, 1, Fin ? QUIC_SEND_FLAG_FIN : QUIC_SEND_FLAG_NONE, SendBuffer))) {
        printf("[strm] StreamSend failed, 0x%x!\n", Status);
        free(SendBufferRaw);
    }
}

void QUICClient::DatagramSend(HQUIC Connection, uint8_t *Data, size_t Length)
{
    if(!IsConnectionOk() || !DatagramSendEnabled) {
        printf("[dagr][%p] Connection Unavailable\n", Connection);
        return;
    }

    QUIC_STATUS Status;
    printf("[dagr][%p] Sending data...\n", Connection);
    // Datagram 전송의 경우 QUIC의 암호화 기능을 제외하고 모두 사용하지 못한다. 
    // StreamSend에서는 주어진 버퍼의 크기가 매우 커도 QUIC에서 알아서 나누어 전송하는 반면
    // DatagramSend에서는 MaxSendLength (MTU)의 크기 이상을 전송할 수 없다.
    // 때문에 애플리케이션에서 알아서 나누어 전송해야한다.
    
    // DTLS와도 유사하다고 보이는데 차이점은 다음과 같다.
    // 1. 계층이 다르다. QUIC - 전송 계층, DTLS - UDP 위에서 동작하는 보안 프로토콜
    // 2. QUIC는 손실탐지 및 혼잡제어를 제공한다.
    while(Length) {
        uint32_t chunk = (uint16_t)(std::min)(Length, (size_t)MaxSendLength);
        // QUIC_BUFFER 구조체의 크기 (포인터 + 길이 값) + 데이터 길이 만큼 할당한다.
        uint8_t* SendBufferRaw = (uint8_t*)malloc(sizeof(QUIC_BUFFER) + chunk);
        if (SendBufferRaw == NULL) {
            printf("[dagr][%p] SendBuffer allocation failed!\n", Connection);
            Status = QUIC_STATUS_OUT_OF_MEMORY;
            return;
        }
        QUIC_BUFFER* SendBuffer = (QUIC_BUFFER*)SendBufferRaw;
        // SendBufferRaw에는 QUIC_BUFFER 구조체 데이터가 포함되어있으므로
        // QUIC_BUFFER 만큼 더해서 포인터를 실제 데이터의 시작 부분을 가리키도록 이동시킨다. 
        SendBuffer->Buffer = SendBufferRaw + sizeof(QUIC_BUFFER);
        // 구조체를 제외한 데이터의 길이
        SendBuffer->Length = chunk;
        memcpy(SendBuffer->Buffer, Data, chunk);
        
        if(QUIC_FAILED(Status = MsQuic->DatagramSend(Connection, SendBuffer, 1, QUIC_SEND_FLAG_NONE, SendBuffer))) {
            printf("[dagr] DatagramSend failed, 0x%x!\n", Status);
            free(SendBufferRaw);
        }

        Length -= chunk;
        Data += chunk;
    }
}

void QUICClient::ConnectionShutdown(HQUIC Connection, bool SendNotify, QUIC_UINT62 ErrorCode)
{
    if(!IsConnectionOk()) {
        printf("[conn] Connection Already Down\n");
        return;
    }
    printf("[conn] Shutdown Connection\n");
    MsQuic->ConnectionShutdown(Connection, SendNotify ? QUIC_CONNECTION_SHUTDOWN_FLAG_NONE : QUIC_CONNECTION_SHUTDOWN_FLAG_SILENT, ErrorCode);
}

void
QUICClient::ClientSend(
    _In_ HQUIC Connection
    )
{
    QUIC_STATUS Status;
    HQUIC Stream = NULL;
    uint8_t* SendBufferRaw;
    QUIC_BUFFER* SendBuffer;

    //
    // Create/allocate a new bidirectional stream. The stream is just allocated
    // and no QUIC stream identifier is assigned until it's started.
    //
    if (QUIC_FAILED(Status = MsQuic->StreamOpen(Connection, QUIC_STREAM_OPEN_FLAG_NONE, ClientStreamCallback, NULL, &Stream))) {
        printf("[strm] StreamOpen failed, 0x%x!\n", Status);
        goto Error;
    }

    printf("[strm][%p] Starting...\n", Stream);

    //
    // Starts the bidirectional stream. By default, the peer is not notified of
    // the stream being started until data is sent on the stream.
    //
    if (QUIC_FAILED(Status = MsQuic->StreamStart(Stream, QUIC_STREAM_START_FLAG_NONE))) {
        printf("[strm] StreamStart failed, 0x%x!\n", Status);
        MsQuic->StreamClose(Stream);
        goto Error;
    }

    //
    // Allocates and builds the buffer to send over the stream.
    //
    SendBufferRaw = (uint8_t*)malloc(sizeof(QUIC_BUFFER) + SendBufferLength);
    if (SendBufferRaw == NULL) {
        printf("[strm] SendBuffer allocation failed!\n");
        Status = QUIC_STATUS_OUT_OF_MEMORY;
        goto Error;
    }
    SendBuffer = (QUIC_BUFFER*)SendBufferRaw;
    SendBuffer->Buffer = SendBufferRaw + sizeof(QUIC_BUFFER);
    SendBuffer->Length = SendBufferLength;

    printf("[strm][%p] Sending data...\n", Stream);

    //
    // Sends the buffer over the stream. Note the FIN flag is passed along with
    // the buffer. This indicates this is the last buffer on the stream and the
    // the stream is shut down (in the send direction) immediately after.
    //
    if (QUIC_FAILED(Status = MsQuic->StreamSend(Stream, SendBuffer, 1, QUIC_SEND_FLAG_FIN, SendBuffer))) {
        printf("[strm] StreamSend failed, 0x%x!\n", Status);
        free(SendBufferRaw);
        goto Error;
    }

Error:

    if (QUIC_FAILED(Status)) {
        MsQuic->ConnectionShutdown(Connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
    }
}


void
QUICClient::EncodeHexBuffer(
    _In_reads_(BufferLen) uint8_t* Buffer,
    _In_ uint8_t BufferLen,
    _Out_writes_bytes_(2*BufferLen) char* HexString
    )
{
    #define HEX_TO_CHAR(x) ((x) > 9 ? ('a' + ((x) - 10)) : '0' + (x))
    for (uint8_t i = 0; i < BufferLen; i++) {
        HexString[i*2]     = HEX_TO_CHAR(Buffer[i] >> 4);
        HexString[i*2 + 1] = HEX_TO_CHAR(Buffer[i] & 0xf);
    }
}

void
QUICClient::WriteSslKeyLogFile(
    _In_z_ const char* FileName,
    _In_ QUIC_TLS_SECRETS* TlsSecrets
    )
{
    printf("[ssl] Writing SSLKEYLOGFILE at %s\n", FileName);
    FILE* File = NULL;
#ifdef _WIN32
    File = _fsopen(FileName, "wb", _SH_DENYNO);
#else
    File = fopen(FileName, "wb");
#endif

    if (File == NULL) {
        printf("[ssl] Failed to open sslkeylogfile %s\n", FileName);
        return;
    }
    // if (fseek(File, 0, SEEK_END) == 0 && ftell(File) == 0) {
        fprintf(File, "# TLS 1.3 secrets log file, generated by msquic\n");
    // }
    

    char ClientRandomBuffer[(2 * sizeof(((QUIC_TLS_SECRETS*)0)->ClientRandom)) + 1] = {0};

    char TempHexBuffer[(2 * QUIC_TLS_SECRETS_MAX_SECRET_LEN) + 1] = {0};
    if (TlsSecrets->IsSet.ClientRandom) {
        EncodeHexBuffer(
            TlsSecrets->ClientRandom,
            (uint8_t)sizeof(TlsSecrets->ClientRandom),
            ClientRandomBuffer);
    }

    if (TlsSecrets->IsSet.ClientEarlyTrafficSecret) {
        EncodeHexBuffer(
            TlsSecrets->ClientEarlyTrafficSecret,
            TlsSecrets->SecretLength,
            TempHexBuffer);
        fprintf(
            File,
            "CLIENT_EARLY_TRAFFIC_SECRET %s %s\n",
            ClientRandomBuffer,
            TempHexBuffer);
    }

    if (TlsSecrets->IsSet.ClientHandshakeTrafficSecret) {
        EncodeHexBuffer(
            TlsSecrets->ClientHandshakeTrafficSecret,
            TlsSecrets->SecretLength,
            TempHexBuffer);
        fprintf(
            File,
            "CLIENT_HANDSHAKE_TRAFFIC_SECRET %s %s\n",
            ClientRandomBuffer,
            TempHexBuffer);
    }

    if (TlsSecrets->IsSet.ServerHandshakeTrafficSecret) {
        EncodeHexBuffer(
            TlsSecrets->ServerHandshakeTrafficSecret,
            TlsSecrets->SecretLength,
            TempHexBuffer);
        fprintf(
            File,
            "SERVER_HANDSHAKE_TRAFFIC_SECRET %s %s\n",
            ClientRandomBuffer,
            TempHexBuffer);
    }

    if (TlsSecrets->IsSet.ClientTrafficSecret0) {
        EncodeHexBuffer(
            TlsSecrets->ClientTrafficSecret0,
            TlsSecrets->SecretLength,
            TempHexBuffer);
        fprintf(
            File,
            "CLIENT_TRAFFIC_SECRET_0 %s %s\n",
            ClientRandomBuffer,
            TempHexBuffer);
    }

    if (TlsSecrets->IsSet.ServerTrafficSecret0) {
        EncodeHexBuffer(
            TlsSecrets->ServerTrafficSecret0,
            TlsSecrets->SecretLength,
            TempHexBuffer);
        fprintf(
            File,
            "SERVER_TRAFFIC_SECRET_0 %s %s\n",
            ClientRandomBuffer,
            TempHexBuffer);
    }

    fflush(File);
    fclose(File);
}
