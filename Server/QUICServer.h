#pragma once
#include <msquic.h>
#include <stdio.h>
#include <cstdlib>

//https://github.com/microsoft/msquic/blob/main/docs/API.md
//https://github.com/microsoft/msquic/blob/main/src/tools/sample/sample.c (예제)

// 중요. 윈도우에서 pem기반 인증서를 사용하려면 openssl을 사용한 msquic로 빌드해야한다.
// 우분투에서는 기본적으로 openssl 기반으로 빌드가 되지만
// 윈도우에서는 별도의 설정이 없다면 Schannel 기반으로 빌드가 된다.
// vcpkg에서 0-rtt를 활성화 하면 openssl 기반으로 빌드가 된다.
#if defined(_WIN32) || defined(_WIN64)
constexpr char Cert[] = "C:\\Users\\byung\\Desktop\\Streaming\\StreamingServer\\Cert\\Server\\Server.crt";
constexpr char KeyFile[] = "C:\\Users\\byung\\Desktop\\Streaming\\StreamingServer\\Cert\\Server\\privkey-Server.pem";
#elif defined(__linux__)
constexpr char Cert[] = "/home/ubuntu/StreamingServer/Cert/Server.crt";
constexpr char KeyFile[] = "/home/ubuntu/StreamingServer/Cert/privkey-Server.pem";
#endif

//log 기록을 위한 환경변수명. 동작을 위해서는 같은 이름의 환경변수를 OS에 등록해주어야한다.
constexpr char                      SslKeyLogEnvVar[] = "SSLKEYLOGFILE";
constexpr QUIC_REGISTRATION_CONFIG  RegConfig = {"Server 1.3", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
const QUIC_BUFFER                   Alpn[] = {
                                        // { sizeof("h3")-1, (uint8_t*)"h3" },          // 정식 HTTP/3
                                        { sizeof("sample")-1, (uint8_t*)"sample" },
                                        { sizeof("test")-1, (uint8_t*)"test" }
                                    };

typedef struct QUIC_CREDENTIAL_CONFIG_HELPER {
    QUIC_CREDENTIAL_CONFIG CredConfig;
    union {
        QUIC_CERTIFICATE_HASH CertHash;
        QUIC_CERTIFICATE_HASH_STORE CertHashStore;
        QUIC_CERTIFICATE_FILE CertFile;
        QUIC_CERTIFICATE_FILE_PROTECTED CertFileProtected;
    };
} QUIC_CREDENTIAL_CONFIG_HELPER;

class QUICServer {
// QUIC API 객체. MSQUIC를 사용하기 위한 모든 함수를 가지고 있다.
const static QUIC_API_TABLE*        MsQuic;
// Registration 객체. 최상위 객체이다. 공식 문서의 그림을 참고.
static HQUIC                        Registration;
// 설정을 위한 객체. TLS 설정 및 QUIC 설정을 담당한다.
static HQUIC                        Configuration;
QUIC_TLS_SECRETS                    ClientSecrets = {0};    

const static uint16_t               UdpPort = 12340;
const static uint64_t               IdleTimeoutMs = 1000;
const static uint32_t               SendBufferLength = 15000;

// 서버가 정상적으로 동작하는지 체크하는 함수
bool Ok = false;

// _In_을 붙이는 이유는 읽기 전용으로 인자를 넘긴다는 뜻이다. Microsoft Source-code Annotation Language(SAL) 참고
static
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS
QUIC_API
ServerStreamCallback(_In_ HQUIC Stream, _In_opt_ void* Context, _Inout_ QUIC_STREAM_EVENT* Event);

//
// The server's callback for connection events from MsQuic.
//
// 클라이언트와 연결되었을 때(QUIC handshake 종료 후) 수행할 동작 정의
static
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS
QUIC_API
ServerConnectionCallback(_In_ HQUIC Connection, _In_opt_ void* Context, _Inout_ QUIC_CONNECTION_EVENT* Event);

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_LISTENER_CALLBACK)
QUIC_STATUS
QUIC_API
ServerListenerCallback(_In_ HQUIC Listener, _In_opt_ void* Context, _Inout_ QUIC_LISTENER_EVENT* Event);

bool SetConfiguration();

static void ServerSend(_In_ HQUIC Stream);

public:
    QUICServer();
    ~QUICServer();

    void ServerStart();

    inline bool IsOk() {return Ok;}
};