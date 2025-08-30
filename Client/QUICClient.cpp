#include "QUICClient.h"

bool QUICClient::SetConfiguration(bool secure)
{
    // https://github.com/microsoft/msquic/blob/main/docs/api/QUIC_SETTINGS.md
    // QUIC 설정 구조체
    QUIC_SETTINGS settings = {0};
    //
    // Configures the client's idle timeout.
    //
    settings.IdleTimeoutMs = IDLE_TIMEOUT_MS;
    settings.IsSet.IdleTimeoutMs = IDLE_TIMEOUT_MS_SET;

    // Datagram 활성화
    settings.DatagramReceiveEnabled = DATAGRAM_RECEIVE_ENABLED;
    settings.IsSet.DatagramReceiveEnabled = DATAGRAM_RECEIVE_ENABLED_SET; 

    //
    // Configures a default client configuration, optionally disabling
    // server certificate validation.
    //
    QUIC_CREDENTIAL_CONFIG cred_config;
    memset(&cred_config, 0, sizeof(cred_config));
    cred_config.Type = QUIC_CREDENTIAL_TYPE_NONE;
    cred_config.Flags = QUIC_CREDENTIAL_FLAG_CLIENT;
    
    if(!secure) {
        cred_config.Flags |= QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
        printf("[config] -------CA Verification disabled.-------\n");
    }
    else {
        // QUIC_CREDENTIAL_FLAG_USE_TLS_BUILTIN_CERTIFICATE_VALIDATION - OpenSSL의 기본 검증을 사용 (SCHANNEL의 경우 무용지물)
        // QUIC_CREDENTIAL_FLAG_SET_CA_CERTIFICATE_FILE - CA 파일을 사용하겠다고 명시
        // 윈도우에서 OpenSSL 사용시 모두 활성화 해줘야한다.
        cred_config.Flags |= QUIC_CREDENTIAL_FLAG_USE_TLS_BUILTIN_CERTIFICATE_VALIDATION
                            | QUIC_CREDENTIAL_FLAG_SET_CA_CERTIFICATE_FILE;
                            
        // CA 파일 지정 (윈도우의 경우)
        cred_config.CaCertificateFile = (char*)ca_file;
        printf("[config] -------CA Verification enabled-------\n");
    }

    //
    // Allocate/initialize the configuration object, with the configured ALPN
    // and settings.
    //
    if (QUIC_FAILED(ms_quic->ConfigurationOpen(registration, alpn, 3, &settings, sizeof(settings), NULL, &configuration))) {
        printf("[config] ConfigurationOpen failed!\n");
        return FALSE;
    }

    //
    // Loads the TLS credential part of the configuration. This is required even
    // on client side, to indicate if a certificate is required or not.
    //
    if (QUIC_FAILED(ms_quic->ConfigurationLoadCredential(configuration, &cred_config))) {
        printf("[config] ConfigurationLoadCredential failed!\n");
        return FALSE;
    }

    return TRUE;
}

QUICClient::QUICClient(bool secure)
{
    // https://github.com/nlohmann/json?tab=readme-ov-file
    // Json CPP
    printf("[init] Using Json library - %s\n", json::meta().dump().c_str());

    if(ms_quic) {
        printf("[init] Client is Already on.\n");
        return;
    }

    // VideoEncoder vid("/home/ubuntu/Sample/sample.mp4");
    // MSQUIC API 테이블을 생성한다. 버전은 1과 2가 있는데 1은 사용 중지되었다.
    // 생성이 잘 되었는지 unsigned int를 반환하는데 이것을 QUIC_FAILED, QUIC_SUCCEEDED에서 확인할 수 있다.
    if(QUIC_FAILED(MsQuicOpen2(&ms_quic))) {
        printf("[init] QUIC Open Fail\n");
        return;
    }

    if(QUIC_FAILED(ms_quic->RegistrationOpen(&reg_config, &registration))) {
        printf("[init] RegistrationOpen Fail\n");
        return;
    }
    // 서버와 연결시 인증서를 검증할 것이라면 true, 그렇지 않다면 false
    // 검증하지 않아도 통신 자체는 암호화 되지만 MITM(Man-in-the-Middle) 공격에 취약해진다.
    if(!SetConfiguration(secure)) {
        return;
    }

    // StreamMap = &StreamManager(MsQuic);
    printf("[init] Client Init Success\n");
    msquic_ok = true;
    // connection_ok = false;
}


QUICClient::~QUICClient()
{
    if(configuration) ms_quic->ConfigurationClose(configuration);

    if(registration) ms_quic->RegistrationClose(registration);

    if(ms_quic) MsQuicClose(ms_quic);

    msquic_ok = false;
    // connection_ok = false;
    printf("[end] Client Closed\n");
}

ClientConnectionContext *QUICClient::GetClientConnectionContext(const char *target_domain, uint16_t target_port)
{
    return new ClientConnectionContext(ms_quic, registration, configuration, target_domain, target_port);
}
