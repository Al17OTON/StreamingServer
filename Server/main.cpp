#include <msquic.h>
#include <stdio.h>
#include <libavcodec/avcodec.h>
#include "VideoEncoder.h"


#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) (void)(P)
#endif

// QUIC API 객체. MSQUIC를 사용하기 위한 모든 함수를 가지고 있다.
const QUIC_API_TABLE* MsQuic = NULL;
// Registration 객체. 최상위 객체이다. 공식 문서의 그림을 참고.
HQUIC Registration;
// 설정을 위한 객체. TLS 설정 및 QUIC 설정을 담당한다.
HQUIC Configuration;
QUIC_TLS_SECRETS ClientSecrets = {0};

//log 기록을 위한 환경변수명. 동작을 위해서는 같은 이름의 환경변수를 OS에 등록해주어야한다.
const char* SslKeyLogEnvVar = "SSLKEYLOGFILE";

// 등록을 할때 관련 설정을 담은 구조체 https://github.com/microsoft/msquic/blob/main/docs/api/QUIC_REGISTRATION_CONFIG.md
// 서버 이름 (NULL 가능), 프로파일을 설정 가능하다. 여기서는 기본값을 사용하였다.
const QUIC_REGISTRATION_CONFIG RegConfig = {"Server 1.3", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
// ALPN 목록 https://datatracker.ietf.org/doc/html/rfc7301#section-3.1
const QUIC_BUFFER Alpn[] = {
    // { sizeof("h3")-1, (uint8_t*)"h3" },          // 정식 HTTP/3
    { sizeof("sample")-1, (uint8_t*)"sample" },
    { sizeof("test")-1, (uint8_t*)"test" }
};
const uint16_t UdpPort = 4567;
const uint64_t IdleTimeoutMs = 1000;
const uint32_t SendBufferLength = 15000;

typedef struct QUIC_CREDENTIAL_CONFIG_HELPER {
    QUIC_CREDENTIAL_CONFIG CredConfig;
    union {
        QUIC_CERTIFICATE_HASH CertHash;
        QUIC_CERTIFICATE_HASH_STORE CertHashStore;
        QUIC_CERTIFICATE_FILE CertFile;
        QUIC_CERTIFICATE_FILE_PROTECTED CertFileProtected;
    };
} QUIC_CREDENTIAL_CONFIG_HELPER;

// _In_을 붙이는 이유는 읽기 전용으로 인자를 넘긴다는 뜻이다. Microsoft Source-code Annotation Language(SAL) 참고
void ServerSend(_In_ HQUIC Stream) {
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

//
// The server's callback for stream events from MsQuic.
//
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS
QUIC_API
ServerStreamCallback(
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
        printf("[strm][%p] Data received\n", Stream);
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        //
        // The peer gracefully shut down its send direction of the stream.
        //
        printf("[strm][%p] Peer shut down\n", Stream);
        ServerSend(Stream);
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

//
// The server's callback for connection events from MsQuic.
//
// 클라이언트와 연결되었을 때(QUIC handshake 종료 후) 수행할 동작 정의
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS
QUIC_API
ServerConnectionCallback(
    _In_ HQUIC Connection,
    _In_opt_ void* Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event
    )
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
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_LISTENER_CALLBACK)
QUIC_STATUS
QUIC_API
ServerListenerCallback(
    _In_ HQUIC Listener,
    _In_opt_ void* Context,
    _Inout_ QUIC_LISTENER_EVENT* Event
    )
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

bool configuration() {
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

    QUIC_CREDENTIAL_CONFIG_HELPER Config;
    memset(&Config, 0, sizeof(Config));
    Config.CredConfig.Flags = QUIC_CREDENTIAL_FLAG_NONE;

    const char* Cert = "/home/ubuntu/StreamingServer/Cert/Server.crt";
    const char* KeyFile = "/home/ubuntu/StreamingServer/Cert/privkey-Server.pem";
    
    Config.CertFile.CertificateFile = (char*)Cert;
    Config.CertFile.PrivateKeyFile = (char*)KeyFile;
    Config.CredConfig.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
    Config.CredConfig.CertificateFile = &Config.CertFile;

    // 설정 적용
    if (QUIC_FAILED(MsQuic->ConfigurationOpen(Registration, Alpn, 2, &Settings, sizeof(Settings), NULL, &Configuration))) {
        printf("ConfigurationOpen failed!\n");
        return FALSE;
    }

    // TLS 로드
    if (QUIC_FAILED(MsQuic->ConfigurationLoadCredential(Configuration, &Config.CredConfig))) {
        printf("ConfigurationLoadCredential failed!\n");
        return FALSE;
    }

    return TRUE;
}

void runServer() {
    HQUIC Listener = NULL;
    QUIC_ADDR Address = {0}; // 내부를 보면 공용체로 sockaddr 구조체들을 가지고 있다.
    QuicAddrSetFamily(&Address, QUIC_ADDRESS_FAMILY_UNSPEC);
    QuicAddrSetPort(&Address, UdpPort);

    // 설정 준비
    if(!configuration()) {
        return;
    }

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


//https://github.com/microsoft/msquic/blob/main/docs/API.md
//https://github.com/microsoft/msquic/blob/main/src/tools/sample/sample.c (예제)
int main()
{
    // VideoEncoder vid("/home/ubuntu/Sample/sample.mp4");
    // MSQUIC API 테이블을 생성한다. 버전은 1과 2가 있는데 1은 사용 중지되었다.
    // 생성이 잘 되었는지 unsigned int를 반환하는데 이것을 QUIC_FAILED, QUIC_SUCCEEDED에서 확인할 수 있다.
    if(QUIC_FAILED(MsQuicOpen2(&MsQuic))) {
        printf("QUIC Open Fail.\n");
        goto Error;
    }

    // 등록을 진행할 수 있다. 보통은 어플리케이션에서 한번만 실행하면 된다고 한다. 
    // 하지만 한 프로세스에서 여러 어플리케이션을 다루는 경우 독립된 환경을 원하면 여러번 호출할 수 있다고 한다.
    // 각 리지스트레이션은 독립된 컨텍스트와 스레드를 가지게 된다고 함.
    if (QUIC_FAILED(MsQuic->RegistrationOpen(&RegConfig, &Registration))) {
        printf("RegistrationOpen failed!\n");
        goto Error;
    }

    runServer();
    
    Error:
        if(MsQuic != NULL) {
            if(Configuration != NULL) {
                MsQuic->ConfigurationClose(Configuration);
            }
            if(Registration != NULL) {
                // 함수가 호출되기전에 사용하고 있는 자원은 전부 해제해야한다. (알아서 관련 데이터를 지워주지 않는 것 같다.)
                // 참고로 이벤트 콜백에서 호출하지 말 것을 설명하고 있는데 만약 그럴경우 데드락에 걸린다.
                MsQuic->RegistrationClose(Registration);
            }
            MsQuicClose(MsQuic);
        }
    
    printf("Server Exited\n");
    return 0;
}