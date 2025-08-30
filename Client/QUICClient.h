#pragma once

#include <msquic.h>
#include <stdio.h>
#include <cstdlib>
#include <share.h>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <nghttp3/nghttp3.h>
#include "ClientConnectionContext.h"

//https://github.com/microsoft/msquic/blob/main/docs/API.md
//https://github.com/microsoft/msquic/blob/main/src/tools/sample/sample.c (예제)

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) (void)(P)
#endif

#if defined(_WIN32) || defined(_WIN64)
// MSQUIC는 OpenSSL를 사용할 경우 ca 파일을 지정해줘야한다고 한다.
// SCHANNEL은 그럴필요 없다고 한다.
// 참고로 윈도우는 기본 SCHANNEL이다. 현재 0-rtt를 활성화했기 때문에 openssl로 설정되어있다.
constexpr char ca_file[] = "C:\\Program Files\\Git\\mingw64\\etc\\ssl\\certs\\ca-bundle.crt";
#elif defined(__linux__)
constexpr char ca_file[] = "/etc/ssl/certs/ca-certificates.crt";
#endif

#define IDLE_TIMEOUT_MS                     3000
#define IDLE_TIMEOUT_MS_SET                 TRUE
#define DATAGRAM_RECEIVE_ENABLED            TRUE
#define DATAGRAM_RECEIVE_ENABLED_SET        TRUE

constexpr QUIC_REGISTRATION_CONFIG  reg_config = {"Server 1.3", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
const QUIC_BUFFER                   alpn[] = {
                                        { sizeof("h3")-1, (uint8_t*)"h3" },          // 정식 HTTP/3
                                        { sizeof("sample")-1, (uint8_t*)"sample" },
                                        { sizeof("test")-1, (uint8_t*)"test" }
                                    };

using json = nlohmann::json;

class QUICClient {
// QUIC API 객체. MSQUIC를 사용하기 위한 모든 함수를 가지고 있다.
const QUIC_API_TABLE*               ms_quic = nullptr;
// Registration 객체. 최상위 객체이다. 공식 문서의 그림을 참고.
HQUIC                               registration;
// 설정을 위한 객체. TLS 설정 및 QUIC 설정을 담당한다.
HQUIC                               configuration;

bool                                msquic_ok;

bool SetConfiguration(bool secure);

public:
    QUICClient(bool secure);
    ~QUICClient();

    ClientConnectionContext* GetClientConnectionContext(_In_z_ const char* target_domain, _In_range_(1, 65535) uint16_t target_port);
    
    inline bool IsOk() {return msquic_ok;}
};