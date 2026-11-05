# Concurrent File Server

Linux의 named pipe와 `fork()`를 이용한 concurrent 파일 서버이다. 

## 파일

- `server.c`: 요청 수신, 자식 프로세스 생성, 파일 읽기/쓰기
- `client.c`: 사용자 입력, 요청 전송, 결과 출력
- `protocol.h`: 요청/응답 구조체와 상수

## 구조

1. 서버가 공용 FIFO `/tmp/os_file_server.fifo`를 생성한다.
2. 클라이언트는 자신의 응답 FIFO를 생성한다.
3. 클라이언트가 공용 FIFO로 파일명, `r/w`, 바이트 수 또는 문자열을 보낸다.
4. 서버가 요청마다 `fork()`한다.
5. 자식 프로세스가 파일 작업 후 클라이언트 응답 FIFO로 결과를 보낸다.


## 컴파일

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -o server server.c
gcc -std=c11 -Wall -Wextra -Wpedantic -o client client.c
```

## 실행

첫 번째 터미널:

```sh
./server
```

두 번째 터미널:

```sh
./client
```

- `w`: 파일에 문자열 쓰기
- `r`: 파일에서 지정한 바이트 수 읽기
- `q`: 클라이언트 종료

