#include "QUICServer.h"

/*
Google Style 코드 컨벤션
타입: PascalCase (MyClass)
함수: PascalCase (DoWork)
변수: snake_case (send_buffer)
멤버: snake_case_ (뒤에 _)
상수: kPascalCase (kDefaultTimeout)
*/
int main() {
    QUICServer server;

    if(server.IsOk()) {
        server.ServerStart();
    }
}