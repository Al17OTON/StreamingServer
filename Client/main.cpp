#include "QUICClient.h"

#define SERVER_DOMAIN "albertlim.duckdns.org"
#define SERVER_PORT 3333
#define CA_VERIFY 1

int main() {
    QUICClient quic_client(CA_VERIFY);
    // HQUIC Connection;
    // HQUIC BidirectionStream;
    // HQUIC UnidirectionStream;

    // Connection = Client.GetConnection(SERVER_DOMAIN, SERVER_PORT);

    // while(!Client.IsConnectionOk());

    // BidirectionStream = Client.GetStream(Connection, StreamTypes::BIDIRECTION);
    // UnidirectionStream = Client.GetStream(Connection, StreamTypes::UNIDIRECTION);

    // Stream 전송 테스트
    // Client.StreamSend(BidirectionStream, (uint8_t*)"Hello", 5, false);
    // Client.StreamSend(BidirectionStream, (uint8_t*)"Hello2", 6, false);
    // Client.StreamSend(BidirectionStream, (uint8_t*)"Hello3", 6, true);

    // Client.StreamSend(UnidirectionStream, (uint8_t*)"uni 1", 5, true);

    // Datagram 전송 테스트
    // size_t Len = 3000;
    // uint8_t* Msg = (uint8_t*)malloc(Len);
    // memset(Msg, 65, 1000);
    // memset(Msg + 1000, 66, 1000);
    // memset(Msg + 2000, 67, 1000);
    // Client.DatagramSend(Connection, Msg, Len);
    // free(Msg);

    // 연결 종료
    // 별도의 종료 없어도 설정된 Timeout값에 의해 자동 종료된다.
    // Sleep(10000);
    // Client.ConnectionShutdown(Connection, true, 0);

    ClientConnectionContext* client_context = quic_client.GetClientConnectionContext(SERVER_DOMAIN, SERVER_PORT);

    while(!client_context->IsConnectionOk());

    client_context->Post((uint8_t*)"client hello", 12, true);
    Sleep(10000);
    client_context->ConnectionShutdown(true, 0);
}