import os
import uvicorn


def main():
    host = os.getenv("HOST", "0.0.0.0")
    port = int(os.getenv("PORT", "8000"))
    uvicorn.run("avatar_server.app:app", host=host, port=port, reload=False)


if __name__ == "__main__":
    main()