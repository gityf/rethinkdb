#ifndef __REPLICATION_PROTOCOL_HPP__
#define __REPLICATION_PROTOCOL_HPP__

#include <boost/function.hpp>

#include "arch/arch.hpp"
#include "concurrency/mutex.hpp"
#include "containers/shared_buf.hpp"
#include "containers/thick_list.hpp"
#include "data_provider.hpp"
#include "replication/net_structs.hpp"
#include "scoped_malloc.hpp"


namespace replication {

template <class T>
struct stream_pair {
    buffered_data_provider_t *stream;
    buffed_data_t<T> data;

    // This uses key_size, which is completely crap.
    stream_pair(weak_buf_t buffer, size_t beg, size_t n, size_t size = 0) : data(buffer, beg) {
        char *p;
        size_t m = sizeof(T) + buffer.get<T>(beg)->key_size;
        stream = new buffered_data_provider_t(size == 0 ? n - m : size, (void **)&p);

        memcpy(p, buffer.get<char>(beg + m), n - m);
    }

    T *operator->() { return data.get(); }
};

class message_callback_t {
public:
    // These could call .swap on their parameter, taking ownership of the pointee.
    virtual void hello(net_hello_t message) = 0;
    virtual void send(buffed_data_t<net_backfill_t>& message) = 0;
    virtual void send(buffed_data_t<net_backfill_complete_t>& message) = 0;
    virtual void send(buffed_data_t<net_announce_t>& message) = 0;
    virtual void send(buffed_data_t<net_get_cas_t>& message) = 0;
    virtual void send(stream_pair<net_sarc_t>& message) = 0;
    virtual void send(stream_pair<net_backfill_set_t>& message) = 0;
    virtual void send(buffed_data_t<net_incr_t>& message) = 0;
    virtual void send(buffed_data_t<net_decr_t>& message) = 0;
    virtual void send(stream_pair<net_append_t>& message) = 0;
    virtual void send(stream_pair<net_prepend_t>& message) = 0;
    virtual void send(buffed_data_t<net_delete_t>& message) = 0;
    virtual void send(buffed_data_t<net_backfill_delete_t>& message) = 0;
    virtual void send(buffed_data_t<net_nop_t>& message) = 0;
    virtual void send(buffed_data_t<net_ack_t>& message) = 0;
    virtual void send(buffed_data_t<net_shutting_down_t>& message) = 0;
    virtual void send(buffed_data_t<net_goodbye_t>& message) = 0;
    virtual void conn_closed() = 0;
};

typedef thick_list<std::pair<boost::function<void ()>, std::pair<char *, size_t> > *, uint32_t> tracker_t;

class message_parser_t {
public:
    message_parser_t() : shutdown_asked_for(false), is_live(false) {}
    ~message_parser_t() {}

    void parse_messages(tcp_conn_t *conn, message_callback_t *receiver);

    struct message_parser_shutdown_callback_t {
        virtual void on_parser_shutdown() = 0;
    protected:
        ~message_parser_shutdown_callback_t() { }
    };
    void shutdown(message_parser_shutdown_callback_t *cb);

    void co_shutdown();

private:
    size_t handle_message(message_callback_t *receiver, weak_buf_t buffer, size_t offset, size_t num_read, tracker_t& streams);
    void do_parse_messages(tcp_conn_t *conn, message_callback_t *receiver);
    void do_parse_normal_messages(tcp_conn_t *conn, message_callback_t *receiver, tracker_t& streams);

    bool shutdown_asked_for; /* were we asked to shutdown (used to ignore connection exceptions */
    bool is_live; /* used to signal the parser when to stop, tells whether it needs to shut down */

    message_parser_shutdown_callback_t *_cb;

    DISABLE_COPYING(message_parser_t);
};

class repli_stream_t : public home_thread_mixin_t {
public:
    repli_stream_t(boost::scoped_ptr<tcp_conn_t>& conn, message_callback_t *recv_callback) : recv_cb_(recv_callback) {
        conn_.swap(conn);
        parser_.parse_messages(conn_.get(), recv_callback);
        mutex_acquisition_t ak(&outgoing_mutex_);
        send_hello(ak);
    }

    ~repli_stream_t() {
        if (conn_) {
            co_shutdown();
        }
    }

    // TODO make this protocol-wise (as in street-wise).
    void co_shutdown() {
        rassert(conn_);
        debugf("repli_stream doing conn_.reset()\n");
        conn_.reset();

        debugf("repli_stream doing parser_.co_shutdown\n");
        parser_.co_shutdown();
        debugf("repli_stream done co_shutdown\n");
    }

    void send(net_backfill_t *msg);
    void send(net_announce_t *msg);
    void send(net_get_cas_t *msg);
    void send(net_sarc_t *msg, const char *key, data_provider_t *value);
    void send(net_backfill_set_t *msg, const char *key, data_provider_t *value);
    void send(net_incr_t *msg);
    void send(net_decr_t *msg);
    void send(net_append_t *msg, const char *key, data_provider_t *value);
    void send(net_prepend_t *msg, const char *key, data_provider_t *value);
    void send(net_delete_t *msg);
    void send(net_backfill_delete_t *msg);

    // TODO remove this
    void send(net_nop_t msg);
    // TODO remove this
    void send(net_ack_t msg);


    void send(net_shutting_down_t *msg);
    void send(net_goodbye_t *msg);
    void close();

private:

    template <class net_struct_type>
    void sendobj(uint8_t msgcode, net_struct_type *msg);

    template <class net_struct_type>
    void sendobj(uint8_t msgcode, net_struct_type *msg, const char *key, data_provider_t *data);

    void send_hello(const mutex_acquisition_t& proof_of_acquisition);

    message_callback_t *recv_cb_;
    mutex_t outgoing_mutex_;
    boost::scoped_ptr<tcp_conn_t> conn_;
    message_parser_t parser_;
};


}  // namespace replication


#endif  // __REPLICATION_PROTOCOL_HPP__
