# Asynchronous Web Server (AWS)

## 📋 Overview

Acest proiect constă în implementarea unui server web de înaltă performanță pentru sistemul de operare Linux, capabil să deservească fișiere în mod asincron. Serverul utilizează tehnici avansate de I/O pentru a maximiza eficiența și a reduce overhead-ul procesorului și al memoriei.

### Key Technical Features:

* **I/O Multiplexing**: Utilizarea API-ului `epoll` pentru gestionarea eficientă a multiplelor conexiuni simultane.
* **Zero-copying**: Transmiterea fișierelor statice prin `sendfile` pentru a evita copierea datelor între kernel-space și user-space.
* **Asynchronous File I/O**: Citirea fișierelor dinamice folosind API-ul `io_setup` / `io_submit` (AIO) pentru a nu bloca execuția serverului.
* **Non-blocking Sockets**: Toate operațiunile pe socket-uri sunt non-blocante pentru a permite scalabilitatea.
* **State Machine**: Fiecare conexiune este gestionată printr-o mașină de stări pentru a urmări progresul transferului HTTP.


## 🏗️ Server Architecture

Serverul deservește fișiere din directorul rădăcină `AWS_DOCUMENT_ROOT`, împărțind conținutul în două categorii:

1. **Static Content (`/static/`)**:
* Destinat fișierelor care nu necesită post-procesare.
* Implementare: `sendfile` (Zero-copy).


2. **Dynamic Content (`/dynamic/`)**:
* Destinat fișierelor care ar putea necesita procesare ulterioară (în contextul temei, acestea sunt citite asincron).
* Implementare: Linux AIO (`io_submit`) + Non-blocking sockets.


### HTTP Implementation:

* Suportă protocolul HTTP 1.1 (subset limitat).
* Coduri de stare: `200 OK` (succes) și `404 Not Found` (cale invalidă).
* Parsarea cererilor este realizată folosind un callback-based `http-parser`.


## 📂 Project Structure

```text
.
├── aws.c               # Implementarea principală a serverului (logică epoll, stări conexiuni)
├── aws.h               # Macro-uri, structuri de date și definiții (AWS_DOCUMENT_ROOT, port-uri)
├── http-parser/        # Parser HTTP extern
├── tests/              # Suita de testare automată
└── Makefile            # Instrucțiuni de compilare

```


## 🛠️ Installation & Testing

### Prerequisites

* Sistem de operare Linux (kernel modern pentru suport `epoll` și `AIO`).
* Compilator `gcc` și utilitarul `make`.

### Compilation

Pentru a compila serverul, rulează următoarea comandă în directorul sursă:

```bash
make

```

### Running Tests

Suita de teste verifică funcționalitatea serverului, utilizarea corectă a API-urilor (sendfile, epoll, io_submit) și eventualele scurgeri de memorie.

```bash
cd tests/
make check

```

Pentru a rula un test specific (ex: testul 13):

```bash
./_test/run_test.sh 13

```

---

## ⚙️ Technical Details

### Connection State Machine

Fiecare structură `connection` menține o stare (`state`) care poate fi:

* `STATE_RECEIVING`: Primirea și parsarea header-ului HTTP.
* `STATE_SENDING_HEADER`: Trimiterea răspunsului HTTP (ex: `HTTP/1.1 200 OK`).
* `STATE_SENDING_DATA`: Trimiterea conținutului propriu-zis al fișierului.
* `STATE_CLOSING`: Curățarea resurselor și închiderea socket-ului.

### Advanced API Used:

* **Multiplexing**: `epoll_create`, `epoll_ctl`, `epoll_wait`.
* **Zero-Copy**: `sendfile`.
* **Async I/O**: `io_setup`, `io_submit`, `io_getevents`, `eventfd`.


## 📝 Performance Notes

Prin combinarea `epoll` (notificări bazate pe evenimente) cu `sendfile` și `AIO`, serverul minimizează numărul de context-switches și operațiunile de copiere a datelor, fiind capabil să gestioneze un volum mare de cereri simultane cu un consum minim de resurse.


**Author:** [Daria-Ioana Drăghici]
**Project:** Operating Systems - Asynchronous Web Server Assignment
