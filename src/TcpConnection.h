#ifndef BOT_TCP_CONNECTION_H
#define BOT_TCP_CONNECTION_H

#include <deque>
#include <string>
#include <vector>
#include <uv.h>
#include <cassert>

namespace bot {

template<class T>
class TcpConnection {
public:
    TcpConnection(T &ready) : ready(ready) {}

    int start(uv_loop_t *loop, struct sockaddr *addr) {
        this->loop = loop;
        int res;
        if ((res = uv_tcp_init(loop, &tcp)) != 0) {
            return res;
        }
        tcp.data = this;
        if ((res = uv_tcp_connect(&req, &tcp, addr, &TcpConnection::callback))) {
            return res;
        }
        req.data = this;
        return 0;
    }

    void write(std::string &&str) {
        write_queue.emplace_back(str);
        begin_write();
    }

    void writef(const char *fmt, ...) {
        va_list ap1, ap2;
        va_start(ap1, fmt);
        ssize_t len = vsnprintf(NULL, 0, fmt, ap1);
        va_end(ap1);
        assert(len >= 0);
        char *buf = new char[len + 1];
        va_start(ap2, fmt);
        vsnprintf(buf, len+1, fmt, ap2);
        va_end(ap2);
        write(buf);
        delete[] buf;
    }

private:
    void begin_write() {
        if (!write_active && !write_queue.empty()) {
            std::vector<uv_buf_t> bufs;
            for (auto &str : write_queue) {
                bufs.emplace_back(uv_buf_t { const_cast<char*>(str.c_str()), str.size() });
            }
            write_num = write_queue.size();
            write_active = true;
            uv_write(&write_req, reinterpret_cast<uv_stream_t*>(&tcp), bufs.data(), bufs.size(),
                     &TcpConnection::on_write);
            write_req.data = this;
        }
    }

    static void callback(uv_connect_t *req, int status) {
        TcpConnection &self = *reinterpret_cast<TcpConnection*>(req->data);
        if (status) {
            self.ready.onError(status);
            return;
        }
        int res;
        if ((res = uv_read_start(reinterpret_cast<uv_stream_t*>(&self.tcp),
                                 &TcpConnection::alloc, &TcpConnection::on_read))) {
            self.ready.onError(res);
            return;
        }
        self.ready.start(self.loop, &self.tcp);
    }

    static void alloc(uv_handle_t *handle, size_t size, uv_buf_t *buf) {
        (void)size;
        TcpConnection &self = *reinterpret_cast<TcpConnection*>(handle->data);
        buf->base = self.buffer;
        buf->len = sizeof(self.buffer);
    }

    static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
        TcpConnection &self = *reinterpret_cast<TcpConnection*>(stream->data);
        if (nread < 0) {
            self.ready.onError(nread);
        } else {
            self.ready.onRead(nread, buf);
        }
    }

    static void on_write(uv_write_t *req, int status) {
        TcpConnection &self = *reinterpret_cast<TcpConnection*>(req->data);
        if (status != 0) {
            self.ready.onError(status);
        } else {
            for (unsigned i = 0; i < self.write_num; i++) {
                self.write_queue.pop_front();
            }
            self.write_num = 0;
        }
        self.write_active = false;
        self.begin_write();
    }

    char buffer[65536] = "";
    std::deque<std::string> write_queue;
    unsigned write_num = 0;
    uv_write_t write_req;
    bool write_active = false;
    uv_tcp_t tcp;
    uv_connect_t req;
    uv_loop_t *loop = nullptr;
    T &ready;
};

}

#endif