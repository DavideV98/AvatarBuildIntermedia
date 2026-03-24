from fastapi import FastAPI
from avatar_server.routes import router

app = FastAPI(
    title="Avatar NeMo STT Server",
    version="3.0",
)

app.include_router(router)