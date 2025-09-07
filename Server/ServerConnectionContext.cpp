#include "ServerConnectionContext.h"
#include "QUICServer.h"

/// @brief Connection의 Stream 객체들을 관리하는 클래스. 생성자에서는 상수값을 설정한다. 후에 연결이 완료되면 ConnectionContextInit()을 호출해야한다.
/// @param ms_quic
/// @param quic_connection
ServerConnectionContext::ServerConnectionContext(const QUIC_API_TABLE *ms_quic, HQUIC quic_connection, uint64_t id)
: ms_quic(ms_quic)
, quic_connection(quic_connection)
, id(id)
{
    stream_map = new StreamMap(ms_quic);
}

ServerConnectionContext::~ServerConnectionContext()
{
    delete stream_map;
    delete http3;
}

/// @brief Connection Complete 이벤트에서 호출 http3에서 필요한 변수들을 할당한다.
/// @return
void ServerConnectionContext::Http3Init()
{
    printf("[http3] Http Init...\n");
    QUIC_STATUS status;

    http3 = new ServerHttp3(this);

    http3_control_stream = GetStream(StreamTypes::UNIDIRECTION);
    http3_qpack_encoder_stream = GetStream(StreamTypes::UNIDIRECTION);
    http3_qpack_decoder_stream = GetStream(StreamTypes::UNIDIRECTION);

    // pending_stream.insert({http3_control_stream, http3});
    // pending_stream.insert({http3_qpack_decoder_stream, http3});
    // pending_stream.insert({http3_qpack_encoder_stream, http3});

    uint32_t size = sizeof(http3_control_stream_id);
    while (QUIC_FAILED(status = ms_quic->GetParam(http3_control_stream, QUIC_PARAM_STREAM_ID, &size, &http3_control_stream_id)))
        ;
    while (QUIC_FAILED(status = ms_quic->GetParam(http3_qpack_encoder_stream, QUIC_PARAM_STREAM_ID, &size, &http3_qpack_encoder_stream_id)))
        ;
    while (QUIC_FAILED(status = ms_quic->GetParam(http3_qpack_decoder_stream, QUIC_PARAM_STREAM_ID, &size, &http3_qpack_decoder_stream_id)))
        ;

    if (!http3->SetControlStreamId(http3_control_stream_id) ||
        !http3->SetQPACKStreamId(http3_qpack_encoder_stream_id, http3_qpack_decoder_stream_id))
    {
        return;
    }
    printf("[http3] http3 stream open success.\n");
    http3_stream_ok = true;
}

/// @brief 스트림 생성 후 반환
/// @param stream_type 양방향, 단방향 여부
/// @return 스트림 객체를 반환한다. 실패한 경우 nullptr 반환
HQUIC ServerConnectionContext::GetStream(StreamTypes stream_type)
{
    QUIC_STATUS status;
    HQUIC stream = NULL;

    if (QUIC_FAILED(status = ms_quic->StreamOpen(quic_connection, stream_type == StreamTypes::BIDIRECTION ? QUIC_STREAM_OPEN_FLAG_NONE : QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL, ServerStreamCallback, this, &stream)))
    {
        printf("[strm] StreamOpen failed, 0x%x!\n", status);
        goto Error;
    }

    printf("[strm][%p] %s Starting...\n", stream, stream_type == StreamTypes::BIDIRECTION ? "Bidirection Stream" : "Unidirection Stream");

    if (QUIC_FAILED(status = ms_quic->StreamStart(stream, QUIC_STREAM_START_FLAG_IMMEDIATE)))
    {
        printf("[strm] StreamStart failed, 0x%x!\n", status);
        ms_quic->StreamClose(stream);
        goto Error;
    }

Error:
    if (QUIC_FAILED(status))
    {
        ms_quic->ConnectionShutdown(quic_connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
        return nullptr;
    }
    return stream;
}

/// @brief 스트림을 열고 Map에 등록 후 스트림 생성 여부 반환
/// @param stream_type 단방향, 양방향 여부 결정
/// @param stream_id 스트림 아이디를 받을 포인터
/// @return 스트림 생성 여부 반환
bool ServerConnectionContext::OpenStream(StreamTypes stream_type, uint64_t &stream_id)
{
    HQUIC stream = GetStream(stream_type);
    if (stream == nullptr)
        return false;

    // stream_id = GetStreamId(stream);
    // StreamElement element(stream, StreamStatus::Idle);

    // stream_map->InsertStream(stream_id, element);
    return true;
}

/// @brief 스트림 콜백 함수
/// @param stream
/// @param context
/// @param event
/// @return
QUIC_STATUS QUIC_API ServerConnectionContext::ServerStreamCallback(HQUIC stream, void *context, QUIC_STREAM_EVENT *event)
{
    ServerConnectionContext *this_context = (ServerConnectionContext *)context;
    StreamCtx *stream_ctx = (StreamCtx *)this_context->ms_quic->GetContext(stream);

    // 처음보는 스트림인 경우
    if (stream_ctx == nullptr)
    {
        stream_ctx = new StreamCtx();
        stream_ctx->ms_quic = this_context->ms_quic;
        stream_ctx->stream = stream;
        this_context->ms_quic->SetContext(stream, stream_ctx);
    }

    uint64_t stream_id = this_context->GetStreamId(stream);
    // uint32_t size = sizeof(stream_id);
    // QUIC_STATUS status = this_context->ms_quic->GetParam(stream, QUIC_PARAM_STREAM_ID, &size, &stream_id);
    // if(!QUIC_FAILED(status)) {
    //     this_context->InsertStreamMap(stream_id, StreamElement(stream, StreamStatus::Idle, nullptr));
    // }


    switch (event->Type)
    {
    case QUIC_STREAM_EVENT_START_COMPLETE:
    {

        printf("[strm][%p] Stream Start Complete. ID : %lu\n", stream, event->START_COMPLETE.ID);
        // ReceiverInterface* receiver = this_context->pending_stream.at(stream);
        // this_context->pending_stream.erase(stream);
        this_context->InsertStreamMap(event->START_COMPLETE.ID, StreamElement(stream, StreamStatus::Idle, nullptr));
        break;
    }
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        //
        // A previous StreamSend call has completed, and the context is being
        // returned back to the app.
        //
        free(event->SEND_COMPLETE.ClientContext);
        printf("[strm][%p] Data sent\n", stream);
        break;
    case QUIC_STREAM_EVENT_RECEIVE:
    {
        //
        // Data was received from the peer on the stream.
        //

        printf("[strm][%p] Data received\n", stream);

        // for(int i = 0; i < event->RECEIVE.BufferCount; ++i) {
        //     const QUIC_BUFFER& b = event->RECEIVE.Buffers[i];
        //     // printf("[srv][%p] %.*s\n", stream, (int)b.Length, (const char*)b.Buffer);
        //     hexdump(b.Buffer, b.Length);
        // }
        // this_context->stream_map->Print();
        // this_context->stream_map->GetStreamById(stream_id)->receiver->Read(stream_id, event);
        long int n = this_context->http3->Read(stream_id, event);
        // if(!stream_ctx->user_data_set && this_context->IsStreamIdAvailable(stream)) {
        //     stream_ctx->user_data_set = true;
        //     this_context->http3->SetContext(stream_id, stream_ctx);
        // }

        if (event->RECEIVE.Flags & QUIC_RECEIVE_FLAG_FIN)
        {
            printf("[srv][%p] <FIN received>\n", stream);
        }

        break;
    }
    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        //
        // The peer gracefully shut down its send direction of the stream.
        //
        printf("[strm][%p] Peer shut down\n", stream);
        // ServerSend(Stream);
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        //
        // The peer aborted its send direction of the stream.
        //
        printf("[strm][%p] Peer aborted\n", stream);
        this_context->ms_quic->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        //
        // Both directions of the stream have been shut down and MsQuic is done
        // with the stream. It can now be safely cleaned up.
        //
        printf("[strm][%p] All done\n", stream);
        this_context->ms_quic->SetContext(stream, nullptr);
        delete stream_ctx;
        this_context->ms_quic->StreamClose(stream);
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API ServerConnectionContext::ServerConnectionCallback(HQUIC connection, void *context, QUIC_CONNECTION_EVENT *event)
{
    ServerConnectionContext *this_context = (ServerConnectionContext *)context;

    switch (event->Type)
    {
    case QUIC_CONNECTION_EVENT_CONNECTED:
    {
        //
        // The handshake has completed for the connection.
        //
        printf("[conn][%p] Connected\n", connection);
        this_context->ms_quic->ConnectionSendResumptionTicket(connection, QUIC_SEND_RESUMPTION_FLAG_NONE, 0, NULL);

        // Stream ID 발급까지 대기하는 스레드 호출
        std::thread http3_init_thread(&ServerConnectionContext::Http3Init, this_context);
        http3_init_thread.detach();

        break;
    }
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        //
        // The connection has been shut down by the transport. Generally, this
        // is the expected way for the connection to shut down with this
        // protocol, since we let idle timeout kill the connection.
        //
        if (event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status == QUIC_STATUS_CONNECTION_IDLE)
        {
            printf("[conn][%p] Successfully shut down on idle.\n", connection);
        }
        else
        {
            printf("[conn][%p] Shut down by transport, 0x%x\n", connection, event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
        }
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        //
        // The connection was explicitly shut down by the peer.
        //
        printf("[conn][%p] Shut down by peer, 0x%llu\n", connection, (unsigned long long)event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        //
        // The connection has completed the shutdown process and is ready to be
        // safely cleaned up.
        //
        printf("[conn][%p] All done\n", connection);
        this_context->ms_quic->ConnectionClose(connection);
        QUICServer::RegisterConnectionRemove(this_context->id);
        break;
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        //
        // The peer has started/created a new stream. The app MUST set the
        // callback handler before returning.
        //
        printf("[strm][%p] Peer started\n", event->PEER_STREAM_STARTED.Stream);
        this_context->InsertStreamMap(this_context->GetStreamId(event->PEER_STREAM_STARTED.Stream), StreamElement(event->PEER_STREAM_STARTED.Stream, StreamStatus::Idle, nullptr));
        this_context->ms_quic->SetCallbackHandler(event->PEER_STREAM_STARTED.Stream, (void *)ServerStreamCallback, context);
        break;
    case QUIC_CONNECTION_EVENT_RESUMED:
        //
        // The connection succeeded in doing a TLS resumption of a previous
        // connection's session.
        //
        printf("[conn][%p] Connection resumed!\n", connection);
        break;
    case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED:
        printf("[dagr] Data Received (%d bytes) : %.*s\n", (int)event->DATAGRAM_RECEIVED.Buffer->Length, (int)event->DATAGRAM_RECEIVED.Buffer->Length, (const char *)event->DATAGRAM_RECEIVED.Buffer->Buffer);
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

bool ServerConnectionContext::IsStreamIdAvailable(HQUIC stream)
{
    uint64_t stream_id = 0;
    uint32_t size = sizeof(stream_id);
    QUIC_STATUS status = ms_quic->GetParam(stream, QUIC_PARAM_STREAM_ID, &size, &stream_id);
    return !QUIC_FAILED(status);
}

HQUIC ServerConnectionContext::GetStreamById(uint64_t stream_id)
{
    return stream_map->GetStreamById(stream_id)->stream;
}

StreamCtx *ServerConnectionContext::SetHttp3Context(uint64_t stream_id)
{
    HQUIC stream = stream_map->GetStreamById(stream_id)->stream;
    StreamCtx *stream_ctx = (StreamCtx *)ms_quic->GetContext(stream);
    if (!stream_ctx->user_data_set)
    {
        stream_ctx->user_data_set = true;
        http3->SetContext(stream_id, stream_ctx);
    }
    else
    {
        printf("[http3] Context is already set\n");
    }
    return stream_ctx;
}
