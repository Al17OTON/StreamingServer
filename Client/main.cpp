#include "QUICClient.h"

#define SERVER_DOMAIN "albertlim.duckdns.org"
#define SERVER_PORT 3333
#define CA_VERIFY 1

int main() {
    QUICClient Client(CA_VERIFY);
    HQUIC Connection;
    HQUIC BidirectionStream;
    HQUIC UnidirectionStream;

    // TODO : 콜백 내부에서 StreamSend() 호출 X, 이벤트를 통해서 상태를 변경하고 연결이 된 시점에 전송하도록 코드 변경하기
    // 이벤트를 기반으로 연결이 된 시점에서 데이터 전송을 시작하도록 변경
    // 콜백 함수는 기존 방식으로 롤백해도 될듯
    Connection = Client.GetConnection(SERVER_DOMAIN, SERVER_PORT);

    while(!Client.IsConnectionOk());

    BidirectionStream = Client.GetStream(Connection, BIDIRECTION);
    UnidirectionStream = Client.GetStream(Connection, UNIDIRECTION);

    // Stream 전송 테스트
    Client.StreamSend(BidirectionStream, (uint8_t*)"Hello", 5, false);
    Client.StreamSend(BidirectionStream, (uint8_t*)"Hello2", 6, false);
    Client.StreamSend(BidirectionStream, (uint8_t*)"Hello3", 6, true);

    Client.StreamSend(UnidirectionStream, (uint8_t*)"uni 1", 5, true);

    // Datagram 전송 테스트
    size_t Len = 3000;
    uint8_t* Msg = (uint8_t*)malloc(Len);
    memset(Msg, 65, 1000);
    memset(Msg + 1000, 66, 1000);
    memset(Msg + 2000, 67, 1000);
    Client.DatagramSend(Connection, Msg, Len);
    free(Msg);

    // 연결 종료
    // 별도의 종료 없어도 설정된 Timeout값에 의해 자동 종료된다.
    Sleep(10000);
    Client.ConnectionShutdown(Connection, true, 0);
}