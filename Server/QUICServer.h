#pragma once
#include <msquic.h>
#include <stdio.h>
#include <cstdlib>
#include "ServerConnectionContext.h"

//https://github.com/microsoft/msquic/blob/main/docs/API.md
//https://github.com/microsoft/msquic/blob/main/src/tools/sample/sample.c (예제)

// 중요. 윈도우에서 pem기반 인증서를 사용하려면 openssl을 사용한 msquic로 빌드해야한다.
// 우분투에서는 기본적으로 openssl 기반으로 빌드가 되지만
// 윈도우에서는 별도의 설정이 없다면 Schannel 기반으로 빌드가 된다.
// vcpkg에서 0-rtt를 활성화 하면 openssl 기반으로 빌드가 된다.
#if defined(_WIN32) || defined(_WIN64)
constexpr char cert[] = PROJECT_ROOT "\\..\\Cert\\Server\\Server.crt";
constexpr char key_file[] = PROJECT_ROOT "\\..\\Cert\\Server\\privkey-Server.pem";
#elif defined(__linux__)
constexpr char cert[] = PROJECT_ROOT "/../Cert/fullchain2.pem";
constexpr char key_file[] = PROJECT_ROOT "/../Cert/privkey2.pem";
#endif

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) (void)(P)
#endif

// Setting 값 - SetConfiguration에서 사용
// SET이 붙은 것은 활성화 여부
// TRUE - 사용자 설정, FALSE - Default 사용
#define IDLE_TIME_OUT_MS                    30000
#define IDLE_TIME_OUT_MS_SET                TRUE

#define SERVER_RESUMPTION_LEVEL             QUIC_SERVER_RESUME_AND_ZERORTT
#define SERVER_RESUMPTION_LEVEL_SET         TRUE

#define PEER_BIDISTREAM_COUNT               64
#define PEER_BIDISTREAM_COUNT_SET           TRUE

#define PEER_UNIDISTREAM_COUNT              64
#define PEER_UNIDISTREAM_COUNT_SET          TRUE

#define DATAGRAM_RECEIVE_ENABLED            TRUE
#define DATAGRAM_RECEIVE_ENABLED_SET        TRUE

// 서버가 사용할 포트
// QUIC가 UDP 기반이므로 UDP로 열어줘야 한다.
#define SERVER_PORT                         12340



//log 기록을 위한 환경변수명. 동작을 위해서는 같은 이름의 환경변수를 OS에 등록해주어야한다.
constexpr char                              ssl_key_log_env_var[] = "SSLKEYLOGFILE";
constexpr QUIC_REGISTRATION_CONFIG          reg_config = {"Server 1.3", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
const QUIC_BUFFER                           alpn[] = {
                                                { sizeof("h3")-1, (uint8_t*)"h3" },          // 정식 HTTP/3
                                                { sizeof("sample")-1, (uint8_t*)"sample" },
                                                { sizeof("test")-1, (uint8_t*)"test" }
                                            };

// enum class StreamTypes : uint8_t {
//         BIDIRECTION,
//         UNIDIRECTION
//     };

typedef struct QUIC_CREDENTIAL_CONFIG_HELPER {
    QUIC_CREDENTIAL_CONFIG cred_config;
    union {
        QUIC_CERTIFICATE_HASH cert_hash;
        QUIC_CERTIFICATE_HASH_STORE cert_hash_store;
        QUIC_CERTIFICATE_FILE cert_file;
        QUIC_CERTIFICATE_FILE_PROTECTED cert_file_protected;
    };
} QUIC_CREDENTIAL_CONFIG_HELPER;

class QUICServer {
// QUIC API 객체. MSQUIC를 사용하기 위한 모든 함수를 가지고 있다.
const QUIC_API_TABLE*               ms_quic;
// Registration 객체. 최상위 객체이다. 공식 문서의 그림을 참고.
HQUIC                               registration;
// 설정을 위한 객체. TLS 설정 및 QUIC 설정을 담당한다.
HQUIC                               configuration;
QUIC_TLS_SECRETS                    client_secrets = {0};    

// 예제에서 사용하는 버퍼 크기
// 실제로는 데이터 크기에 맞추어 버퍼를 정한다.
const static uint32_t               send_buffer_length = 15000;

// 서버가 정상적으로 동작하는지 체크하는 함수
bool ok = false;

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_LISTENER_CALLBACK)
QUIC_STATUS
QUIC_API
ServerListenerCallback(_In_ HQUIC listener, _In_opt_ void* context, _Inout_ QUIC_LISTENER_EVENT* event);

bool SetConfiguration();

static void ServerSend(_In_ HQUIC stream, void* context);

public:
    QUICServer();
    ~QUICServer();

    void ServerStart();

    inline bool IsOk() {return ok;}
};