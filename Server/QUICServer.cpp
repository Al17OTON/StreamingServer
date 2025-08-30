#include "QUICServer.h"
#include "ConnectionContext.h"

QUICServer::QUICServer()
{
    if(ok) {
        printf("Server is Already on.\n");
        return;
    }
    // VideoEncoder vid("/home/ubuntu/Sample/sample.mp4");
    // MSQUIC API 테이블을 생성한다. 버전은 1과 2가 있는데 1은 사용 중지되었다.
    // 생성이 잘 되었는지 unsigned int를 반환하는데 이것을 QUIC_FAILED, QUIC_SUCCEEDED에서 확인할 수 있다.
    if(QUIC_FAILED(MsQuicOpen2(&ms_quic))) {
        printf("QUIC Open Fail\n");
        return;
    }

    if(QUIC_FAILED(ms_quic->RegistrationOpen(&reg_config, &registration))) {
        printf("RegistrationOpen Fail\n");
        return;
    }
    
    if(!SetConfiguration()) {
        return;
    }

    // printf("Server Started and Listening on Port : %d\n", UdpPort);
    printf("Server Init Success\n");
    ok = true;
}

// listener 콜백함수
// 클라이언트가 연결을 시도할 때 할 동작을 정의한다.
QUIC_STATUS QUIC_API QUICServer::ServerListenerCallback(HQUIC listener, void *context, QUIC_LISTENER_EVENT *event)
{
    QUICServer* this_context = (QUICServer*)context;
    UNREFERENCED_PARAMETER(listener);
    QUIC_STATUS status = QUIC_STATUS_NOT_SUPPORTED;
    switch (event->Type) {
    case QUIC_LISTENER_EVENT_NEW_CONNECTION:
        //
        // A new connection is being attempted by a client. For the handshake to
        // proceed, the server must provide a configuration for QUIC to use. The
        // app MUST set the callback handler before returning.
        //
        // 콜백을 지정된 HQUIC 객체에 적용한다. (리스너, 스트림, 등등)
        this_context->ms_quic->SetCallbackHandler(event->NEW_CONNECTION.Connection, (void*)ConnectionContext::ServerConnectionCallback, new ConnectionContext(this_context->ms_quic, event->NEW_CONNECTION.Connection));
        // 클라이언트가 연결을 시도하고 있으므로 QUIC Handshake를 하도록 설정한다.
        // QUIC_LISTENER_EVENT_NEW_CONNECTION 내부에서 사용할 것을 권장하며 
        // 어떠한 설정을 사용할지 결정하는 비동기 작업을 수행 등과 같은 경우에는 외부에서 호출해도 된다고 한다.
        status = this_context->ms_quic->ConnectionSetConfiguration(event->NEW_CONNECTION.Connection, this_context->configuration);
        break;
    default:
        break;
    }
    return status;
}

bool QUICServer::SetConfiguration()
{
    QUIC_SETTINGS settings = {0};
    // timeout 설정
    settings.IdleTimeoutMs = IDLE_TIME_OUT_MS;
    settings.IsSet.IdleTimeoutMs = IDLE_TIME_OUT_MS_SET;
    // 0 RTT 설정
    settings.ServerResumptionLevel = SERVER_RESUMPTION_LEVEL;
    settings.IsSet.ServerResumptionLevel = SERVER_RESUMPTION_LEVEL_SET;
    // 양방향 연결을 허용한다. 기본적으로는 peer로부터의 스트림을 허용하지 않는다.
    settings.PeerBidiStreamCount = PEER_BIDISTREAM_COUNT;
    settings.IsSet.PeerBidiStreamCount = PEER_BIDISTREAM_COUNT_SET;

    // 단방향 통신 연결 허용
    settings.PeerUnidiStreamCount = PEER_UNIDISTREAM_COUNT;
    settings.IsSet.PeerUnidiStreamCount = PEER_UNIDISTREAM_COUNT_SET;

    // Datagram 활성화
    settings.DatagramReceiveEnabled = DATAGRAM_RECEIVE_ENABLED;
    settings.IsSet.DatagramReceiveEnabled = DATAGRAM_RECEIVE_ENABLED_SET;

    QUIC_CREDENTIAL_CONFIG_HELPER config;
    memset(&config, 0, sizeof(config));
    config.cred_config.Flags = QUIC_CREDENTIAL_FLAG_NONE;

    config.cert_file.CertificateFile = (char*)cert;
    config.cert_file.PrivateKeyFile = (char*)key_file;
    config.cred_config.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
    config.cred_config.CertificateFile = &config.cert_file;

    // 설정 적용
    QUIC_STATUS status;
    if (QUIC_FAILED(status = ms_quic->ConfigurationOpen(registration, alpn, 3, &settings, sizeof(settings), NULL, &configuration))) {
        printf("ConfigurationOpen failed! code : 0x%x\n", status);
        return false;
    }

    // TLS 로드
    if (QUIC_FAILED(status = ms_quic->ConfigurationLoadCredential(configuration, &config.cred_config))) {
        printf("ConfigurationLoadCredential failed! code : 0x%x\n", status);
        return false;
    }

    return true;
}

void QUICServer::ServerSend(HQUIC stream, void* context)
{
    QUICServer* this_context = (QUICServer*)context;
    //
    // Allocates and builds the buffer to send over the stream.
    //
    // QUIC_BUFFER 구조체의 크기 (포인터 + 길이 값) + 데이터 길이 만큼 할당한다.
    void* send_buffer_raw = malloc(sizeof(QUIC_BUFFER) + send_buffer_length);
    memset(send_buffer_raw, 0, sizeof(sizeof(QUIC_BUFFER) + send_buffer_length));

    if (send_buffer_raw == NULL) {
        printf("SendBuffer allocation failed!\n");
        this_context->ms_quic->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
        return;
    }
    QUIC_BUFFER* send_buffer = (QUIC_BUFFER*)send_buffer_raw;
    // SendBufferRaw에는 QUIC_BUFFER 구조체 데이터가 포함되어있으므로
    // QUIC_BUFFER 만큼 더해서 포인터를 데이터의 시작 부분을 가리키도록 이동시킨다.
    send_buffer->Buffer = (uint8_t*)send_buffer_raw + sizeof(QUIC_BUFFER);
    // 테스트를 위해 모든 비트를 1로 채우기. 마지막 바이트만 제외 (2000바이트가 전송되는지 체크)
    memset(send_buffer->Buffer, -1, send_buffer_length - 1);
    send_buffer->Buffer[send_buffer_length - 1] = 123;
    // 구조체를 제외한 데이터의 길이
    send_buffer->Length = send_buffer_length;

    printf("[strm][%p] Sending data...\n", stream);

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
    QUIC_STATUS status;
    if (QUIC_FAILED(status = this_context->ms_quic->StreamSend(stream, send_buffer, 1, QUIC_SEND_FLAG_FIN, send_buffer))) {
        printf("StreamSend failed, 0x%x!\n", status);
        free(send_buffer_raw);
        this_context->ms_quic->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
    }
}

QUICServer::~QUICServer()
{
    if(configuration) ms_quic->ConfigurationClose(configuration);

    if(registration) ms_quic->RegistrationClose(registration);

    if(ms_quic) MsQuicClose(ms_quic);

    ok = false;
    printf("Server Closed\n");
}

void QUICServer::ServerStart()
{
    HQUIC listener = NULL;
    QUIC_ADDR address = {0}; // 내부를 보면 공용체로 sockaddr 구조체들을 가지고 있다.
    QuicAddrSetFamily(&address, QUIC_ADDRESS_FAMILY_UNSPEC);
    QuicAddrSetPort(&address, SERVER_PORT);

    // 리스너 객체 할당
    // 서버가 quic 통신을 받기 위해서는 ListenerOpen을 통해 관련 자원을 할당해야한다.
    // open만으로는 통신을 받을 수는 없고 start를 통해 실제로 통신을 받을 수 있다고 한다.
    // 그외에도 stop으로 통신을 중지할 수 있고 close를 통해 자원을 해제할 수 있다.
    // 인자값 중에 콜백과 콘텍스트가 있는데 콜백은 통신에서 수행할 작업을 정의할 수 있고
    // 콘텍스트의 경우는 void*이며 콜백의 인자로 넘겨줄 값이다. (NULL 가능)
    // 콜백에서 필요한 상태들을 넘겨 줄 수 있다.
    if (QUIC_FAILED(ms_quic->ListenerOpen(registration, ServerListenerCallback, this, &listener))) {
        printf("ListenerOpen failed!\n");
        goto Error;
    }

    // 실제로 통신을 받도록 설정
    // 마지막 인자는 sockaddr 구조체이다. NULL로 해도 알아서 할당해준다고 한다.
    // 두 번째 인자는 ALPN 목록인데 TLS 핸드셰이크 처음에 서버가 클라이언트로 전송하는 프로토콜 목록이다.
    // 적어도 하나 이상의 프로토콜을 제공해야한다.
    if (QUIC_FAILED(ms_quic->ListenerStart(listener, alpn, 2, &address))) {
        printf("ListenerStart failed!\n");
        goto Error;
    }

    printf("Server Started and Listening on Port : %d\n", SERVER_PORT);
    //
    // Continue listening for connections until the Enter key is pressed.
    //
    printf("Press Enter to exit.\n\n");
    (void)getchar();

    Error:
        if (listener != NULL) {
            // 통신 수신을 종료한다. Stop이 호출되지 않았더라도 Close에서 같이 수행해준다.
            // Listener가 가장 마지막에 호출하는 함수여야한다. 이 후에 listener의 다른 함수를 호출하면 에러가 발생한다.
            // 콜백에서 Close를 호출할 경우 데드락에 빠질 수 있다. 다만, 이벤트가 QUIC_LISTENER_EVENT_STOP_COMPLETE일 경우에는 호출해도 괜찮다.
            ms_quic->ListenerClose(listener);
        }
}