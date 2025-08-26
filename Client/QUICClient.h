#pragma once
#include <msquic.h>
#include <stdio.h>
#include <cstdlib>
#include <share.h>
#include <algorithm>
#include <nlohmann/json.hpp>
//https://github.com/microsoft/msquic/blob/main/docs/API.md
//https://github.com/microsoft/msquic/blob/main/src/tools/sample/sample.c (예제)

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) (void)(P)
#endif

#if defined(_WIN32) || defined(_WIN64)
// MSQUIC는 OpenSSL를 사용할 경우 ca 파일을 지정해줘야한다고 한다.
// SCHANNEL은 그럴필요 없다고 한다.
// 참고로 윈도우는 기본 SCHANNEL이다. 현재 0-rtt를 활성화했기 때문에 openssl로 설정되어있다.
constexpr char CAFile[] = "C:\\Program Files\\Git\\mingw64\\etc\\ssl\\certs\\ca-bundle.crt";
#elif defined(__linux__)
constexpr char CAFile[] = "/etc/ssl/certs/ca-certificates.crt";
#endif

//log 기록을 위한 환경변수명. 동작을 위해서는 같은 이름의 환경변수를 OS에 등록해주어야한다.
constexpr char                      SslKeyLogEnvVar[] = "SSLKEYLOGFILE";
constexpr QUIC_REGISTRATION_CONFIG  RegConfig = {"Server 1.3", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
const QUIC_BUFFER                   Alpn[] = {
                                        // { sizeof("h3")-1, (uint8_t*)"h3" },          // 정식 HTTP/3
                                        { sizeof("sample")-1, (uint8_t*)"sample" },
                                        { sizeof("test")-1, (uint8_t*)"test" }
                                    };

enum StreamTypes {
        BIDIRECTION,
        UNIDIRECTION
    };

using JSON = nlohmann::json;

class QUICClient {
// QUIC API 객체. MSQUIC를 사용하기 위한 모든 함수를 가지고 있다.
const static QUIC_API_TABLE*        MsQuic;
// Registration 객체. 최상위 객체이다. 공식 문서의 그림을 참고.
static HQUIC                        Registration;
// 설정을 위한 객체. TLS 설정 및 QUIC 설정을 담당한다.
static HQUIC                        Configuration;
static QUIC_TLS_SECRETS             ClientSecrets;    

// char*                               Target;
// uint16_t                            UdpPort;    

const static uint64_t               IdleTimeoutMs = 30000;
const static uint32_t               SendBufferLength = 15000;

static bool                         Ok;
static bool                         ConnectionOk;
static bool                         DatagramSendEnabled;
static uint32_t                     MaxSendLength;

const char*                         ResumptionTicketString = NULL;
const char*                         SslKeyLogFile = getenv(SslKeyLogEnvVar);

static
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS
QUIC_API
ClientConnectionCallback(
    _In_ HQUIC Connection,
    _In_opt_ void* Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event
    );

static
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS
QUIC_API
QUICClient::ClientStreamCallback(
    _In_ HQUIC Stream,
    _In_opt_ void* Context,
    _Inout_ QUIC_STREAM_EVENT* Event
    );

static
void
EncodeHexBuffer(
    _In_reads_(BufferLen) uint8_t* Buffer,
    _In_ uint8_t BufferLen,
    _Out_writes_bytes_(2*BufferLen) char* HexString
    );


bool SetConfiguration(bool Secure);

static void WriteSslKeyLogFile(
        _In_z_ const char* FileName,
        _In_ QUIC_TLS_SECRETS* TlsSecrets
        );

public:
    QUICClient(bool Secure);
    ~QUICClient();

    void ClientSend(_In_ HQUIC Connection);
    
    HQUIC GetStream(_In_ HQUIC Connection, StreamTypes StreamType);
    HQUIC GetConnection(_In_z_ const char* Target, _In_range_(1, 65535) uint16_t Port);
    void StreamSend(_In_ HQUIC Stream, uint8_t* Data, size_t Length, bool Fin);
    void DatagramSend(_In_ HQUIC Connection, uint8_t* Data, size_t Length);
    void ConnectionShutdown(_In_ HQUIC Connection, bool SendNotify, QUIC_UINT62 ErrorCode);

    JSON get();
    
    inline bool IsConnectionOk() {return ConnectionOk;}
    inline bool IsOk() {return Ok;}
};