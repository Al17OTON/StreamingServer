#include "QUICServer.h"

const QUIC_API_TABLE* QUICServer::MsQuic      = nullptr;
HQUIC                QUICServer::Registration = nullptr;
HQUIC                QUICServer::Configuration = nullptr;

QUICServer::QUICServer()
{
    if(Ok) {
        printf("Server is Already on.\n");
        return;
    }
    // VideoEncoder vid("/home/ubuntu/Sample/sample.mp4");
    // MSQUIC API 테이블을 생성한다. 버전은 1과 2가 있는데 1은 사용 중지되었다.
    // 생성이 잘 되었는지 unsigned int를 반환하는데 이것을 QUIC_FAILED, QUIC_SUCCEEDED에서 확인할 수 있다.
    if(QUIC_FAILED(MsQuicOpen2(&MsQuic))) {
        printf("QUIC Open Fail\n");
        return;
    }

    if(QUIC_FAILED(MsQuic->RegistrationOpen(&RegConfig, &Registration))) {
        printf("RegistrationOpen Fail\n");
        return;
    }
    
    if(!SetConfiguration()) {
        return;
    }

    // printf("Server Started and Listening on Port : %d\n", UdpPort);
    printf("Server Init Success\n");
    Ok = true;
}

QUIC_STATUS QUIC_API QUICServer::ServerStreamCallback(HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event)
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
        printf("[strm][%p] Data received\n", Stream);
        
        for(int i = 0; i < Event->RECEIVE.BufferCount; ++i) {
            const QUIC_BUFFER& b = Event->RECEIVE.Buffers[i];
            printf("[srv][%p] %.*s\n", Stream, (int)b.Length, (const char*)b.Buffer);
        }
        if (Event->RECEIVE.Flags & QUIC_RECEIVE_FLAG_FIN) {
            printf("\n[srv][%p] <FIN received>\n", Stream);
        }

        break;
    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        //
        // The peer gracefully shut down its send direction of the stream.
        //
        printf("[strm][%p] Peer shut down\n", Stream);
        // ServerSend(Stream);
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        //
        // The peer aborted its send direction of the stream.
        //
        printf("[strm][%p] Peer aborted\n", Stream);
        MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        //
        // Both directions of the stream have been shut down and MsQuic is done
        // with the stream. It can now be safely cleaned up.
        //
        printf("[strm][%p] All done\n", Stream);
        MsQuic->StreamClose(Stream);
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API QUICServer::ServerConnectionCallback(HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event)
{
    UNREFERENCED_PARAMETER(Context);
    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        //
        // The handshake has completed for the connection.
        //
        printf("[conn][%p] Connected\n", Connection);
        MsQuic->ConnectionSendResumptionTicket(Connection, QUIC_SEND_RESUMPTION_FLAG_NONE, 0, NULL);
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
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        //
        // The connection was explicitly shut down by the peer.
        //
        printf("[conn][%p] Shut down by peer, 0x%llu\n", Connection, (unsigned long long)Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        //
        // The connection has completed the shutdown process and is ready to be
        // safely cleaned up.
        //
        printf("[conn][%p] All done\n", Connection);
        MsQuic->ConnectionClose(Connection);
        break;
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        //
        // The peer has started/created a new stream. The app MUST set the
        // callback handler before returning.
        //
        printf("[strm][%p] Peer started\n", Event->PEER_STREAM_STARTED.Stream);
        MsQuic->SetCallbackHandler(Event->PEER_STREAM_STARTED.Stream, (void*)ServerStreamCallback, NULL);
        break;
    case QUIC_CONNECTION_EVENT_RESUMED:
        //
        // The connection succeeded in doing a TLS resumption of a previous
        // connection's session.
        //
        printf("[conn][%p] Connection resumed!\n", Connection);
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

// listener 콜백함수
// 클라이언트가 연결을 시도할 때 할 동작을 정의한다.
QUIC_STATUS QUIC_API QUICServer::ServerListenerCallback(HQUIC Listener, void *Context, QUIC_LISTENER_EVENT *Event)
{
    UNREFERENCED_PARAMETER(Listener);
    UNREFERENCED_PARAMETER(Context);
    QUIC_STATUS Status = QUIC_STATUS_NOT_SUPPORTED;
    switch (Event->Type) {
    case QUIC_LISTENER_EVENT_NEW_CONNECTION:
        //
        // A new connection is being attempted by a client. For the handshake to
        // proceed, the server must provide a configuration for QUIC to use. The
        // app MUST set the callback handler before returning.
        //
        // 콜백을 지정된 HQUIC 객체에 적용한다. (리스너, 스트림, 등등)
        MsQuic->SetCallbackHandler(Event->NEW_CONNECTION.Connection, (void*)ServerConnectionCallback, NULL);
        // 클라이언트가 연결을 시도하고 있으므로 QUIC Handshake를 하도록 설정한다.
        // QUIC_LISTENER_EVENT_NEW_CONNECTION 내부에서 사용할 것을 권장하며 
        // 어떠한 설정을 사용할지 결정하는 비동기 작업을 수행 등과 같은 경우에는 외부에서 호출해도 된다고 한다.
        Status = MsQuic->ConnectionSetConfiguration(Event->NEW_CONNECTION.Connection, Configuration);
        break;
    default:
        break;
    }
    return Status;
}

bool QUICServer::SetConfiguration()
{
    QUIC_SETTINGS Settings = {0};
    // timeout 설정
    Settings.IdleTimeoutMs = IdleTimeoutMs;
    Settings.IsSet.IdleTimeoutMs = TRUE;
    // 0 RTT 설정
    Settings.ServerResumptionLevel = QUIC_SERVER_RESUME_AND_ZERORTT;
    Settings.IsSet.ServerResumptionLevel = TRUE;
    // 양방향 연결을 허용한다. 기본적으로는 peer로부터의 스트림을 허용하지 않는다.
    Settings.PeerBidiStreamCount = 1;
    Settings.IsSet.PeerBidiStreamCount = TRUE;

    // Datagram 활성화
    Settings.DatagramReceiveEnabled = TRUE;
    Settings.IsSet.DatagramReceiveEnabled = TRUE;

    QUIC_CREDENTIAL_CONFIG_HELPER Config;
    memset(&Config, 0, sizeof(Config));
    Config.CredConfig.Flags = QUIC_CREDENTIAL_FLAG_NONE;

    Config.CertFile.CertificateFile = (char*)Cert;
    Config.CertFile.PrivateKeyFile = (char*)KeyFile;
    Config.CredConfig.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
    Config.CredConfig.CertificateFile = &Config.CertFile;

    // 설정 적용
    QUIC_STATUS Status;
    if (QUIC_FAILED(Status = MsQuic->ConfigurationOpen(Registration, Alpn, 2, &Settings, sizeof(Settings), NULL, &Configuration))) {
        printf("ConfigurationOpen failed! code : 0x%x\n", Status);
        return false;
    }

    // TLS 로드
    if (QUIC_FAILED(Status = MsQuic->ConfigurationLoadCredential(Configuration, &Config.CredConfig))) {
        printf("ConfigurationLoadCredential failed! code : 0x%x\n", Status);
        return false;
    }

    return true;
}

void QUICServer::ServerSend(HQUIC Stream)
{
    //
    // Allocates and builds the buffer to send over the stream.
    //
    // QUIC_BUFFER 구조체의 크기 (포인터 + 길이 값) + 데이터 길이 만큼 할당한다.
    void* SendBufferRaw = malloc(sizeof(QUIC_BUFFER) + SendBufferLength);
    memset(SendBufferRaw, 0, sizeof(sizeof(QUIC_BUFFER) + SendBufferLength));

    if (SendBufferRaw == NULL) {
        printf("SendBuffer allocation failed!\n");
        MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
        return;
    }
    QUIC_BUFFER* SendBuffer = (QUIC_BUFFER*)SendBufferRaw;
    // SendBufferRaw에는 QUIC_BUFFER 구조체 데이터가 포함되어있으므로
    // QUIC_BUFFER 만큼 더해서 포인터를 데이터의 시작 부분을 가리키도록 이동시킨다.
    SendBuffer->Buffer = (uint8_t*)SendBufferRaw + sizeof(QUIC_BUFFER);
    // 테스트를 위해 모든 비트를 1로 채우기. 마지막 바이트만 제외 (2000바이트가 전송되는지 체크)
    memset(SendBuffer->Buffer, -1, SendBufferLength - 1);
    SendBuffer->Buffer[SendBufferLength - 1] = 123;
    // 구조체를 제외한 데이터의 길이
    SendBuffer->Length = SendBufferLength;

    printf("[strm][%p] Sending data...\n", Stream);

    //
    // Sends the buffer over the stream. Note the FIN flag is passed along with
    // the buffer. This indicates this is the last buffer on the stream and the
    // the stream is shut down (in the send direction) immediately after.
    //

    // 데이터를 전송한다. 버퍼를 큐에 넣어주기만 하므로 non-blocking이다.  
    // 두 번째 인자는 NULL일 수 있다. (세 번째 인자가 0일 때에만)
    // 네 번째 인자는 데이터의 설정 나타낸다. (손실 시 재전송 여부, 마지막 데이터 여부, 0-RTT 가능 등)
    // 마지막 인자는 ClientContext를 나타내는데 공식 문서에서 자세한 설명은 없다.
    // 추측하건데, 현재 예제에서 SendBuffer를 malloc하는데 free를 하는 코드가 현재 함수에서는 존재하지 않는다.
    // 전송을 완료하고 나서 StreamSend에서 알아서 해제를 진행하는지에 대한 설명이 없다. 공식문서에서는 buffer를 한번 큐에 넣으면
    // QUIC_STREAM_EVENT_SEND_COMPLETE 전까지는 앱에서 수정을 하지 말아야한다고 명시하고 메모리 해제에 대한 설명이 없는데 예제를 좀 더 거슬러 올라가면
    // StreamCallback 함수에서 QUIC_STREAM_EVENT_SEND_COMPLETE 부분에 clientContext의 메모리를 해제하고 있다.
    // 아무래도 네 번째 인자(전송할 필요없는 데이터도 포함되어있는 전체적인 객체), 두 번째 인자(실제로 전송할 데이터 객체)
    // 로 사용하면서 QUIC_STREAM_EVENT_SEND_COMPLETE에서 제거할 수 있도록 하는 것 같다.
    QUIC_STATUS Status;
    if (QUIC_FAILED(Status = MsQuic->StreamSend(Stream, SendBuffer, 1, QUIC_SEND_FLAG_FIN, SendBuffer))) {
        printf("StreamSend failed, 0x%x!\n", Status);
        free(SendBufferRaw);
        MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
    }
}

QUICServer::~QUICServer()
{
    if(Configuration) MsQuic->ConfigurationClose(Configuration);

    if(Registration) MsQuic->RegistrationClose(Registration);

    if(MsQuic) MsQuicClose(MsQuic);

    Ok = false;
    printf("Server Closed\n");
}

void QUICServer::ServerStart()
{
    HQUIC Listener = NULL;
    QUIC_ADDR Address = {0}; // 내부를 보면 공용체로 sockaddr 구조체들을 가지고 있다.
    QuicAddrSetFamily(&Address, QUIC_ADDRESS_FAMILY_UNSPEC);
    QuicAddrSetPort(&Address, UdpPort);

    // 리스너 객체 할당
    // 서버가 quic 통신을 받기 위해서는 ListenerOpen을 통해 관련 자원을 할당해야한다.
    // open만으로는 통신을 받을 수는 없고 start를 통해 실제로 통신을 받을 수 있다고 한다.
    // 그외에도 stop으로 통신을 중지할 수 있고 close를 통해 자원을 해제할 수 있다.
    // 인자값 중에 콜백과 콘텍스트가 있는데 콜백은 통신에서 수행할 작업을 정의할 수 있고
    // 콘텍스트의 경우는 void*이며 콜백의 인자로 넘겨줄 값이다. (NULL 가능)
    // 콜백에서 필요한 상태들을 넘겨 줄 수 있다.
    if (QUIC_FAILED(MsQuic->ListenerOpen(Registration, ServerListenerCallback, NULL, &Listener))) {
        printf("ListenerOpen failed!\n");
        goto Error;
    }

    // 실제로 통신을 받도록 설정
    // 마지막 인자는 sockaddr 구조체이다. NULL로 해도 알아서 할당해준다고 한다.
    // 두 번째 인자는 ALPN 목록인데 TLS 핸드셰이크 처음에 서버가 클라이언트로 전송하는 프로토콜 목록이다.
    // 적어도 하나 이상의 프로토콜을 제공해야한다.
    if (QUIC_FAILED(MsQuic->ListenerStart(Listener, Alpn, 2, &Address))) {
        printf("ListenerStart failed!\n");
        goto Error;
    }

    printf("Server Started and Listening on Port : %d\n", UdpPort);
    //
    // Continue listening for connections until the Enter key is pressed.
    //
    printf("Press Enter to exit.\n\n");
    (void)getchar();

    Error:
        if (Listener != NULL) {
            // 통신 수신을 종료한다. Stop이 호출되지 않았더라도 Close에서 같이 수행해준다.
            // Listener가 가장 마지막에 호출하는 함수여야한다. 이 후에 listener의 다른 함수를 호출하면 에러가 발생한다.
            // 콜백에서 Close를 호출할 경우 데드락에 빠질 수 있다. 다만, 이벤트가 QUIC_LISTENER_EVENT_STOP_COMPLETE일 경우에는 호출해도 괜찮다.
            MsQuic->ListenerClose(Listener);
        }
}
