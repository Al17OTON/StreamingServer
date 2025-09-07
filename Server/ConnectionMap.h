#pragma once

#include <iostream>
#include <atomic>
#include <unordered_map>
#include <queue>
#include "ServerConnectionContext.h"

class ConnectionMap {
private:
    std::unordered_map<uint64_t, void*>                         connection_map;
    std::atomic<uint64_t>                                       connection_id_generator{1};
    std::queue<uint64_t>                                        connection_close_q;
public:

    uint64_t GenerateConnectionId() {
        return connection_id_generator.fetch_add(1, std::memory_order_relaxed);
    }

    /// @brief 생성된 Connection Context를 발급받은 id에 매핑
    /// @param key 
    /// @param value 
    void InsertConnection(uint64_t key, void* value) {
        connection_map.insert({key, value});
    }

    /// @brief 종료할 Connection 등록
    /// @param key 종료할 Connection id
    void RegisterConnectionRemove(uint64_t key) {
        connection_close_q.push(key);
    }

    /// @brief 종료된 Connection Context 제거
    void ConnectionCloseJob() {
        while(!connection_close_q.empty()) {
            uint64_t id = connection_close_q.front();
            connection_close_q.pop();
            ServerConnectionContext* context = (ServerConnectionContext*)connection_map.at(id);
            connection_map.erase(id);
            delete context;
        }
    }

};