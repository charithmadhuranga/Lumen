# Lumen Cloud API image. Pinned base images (AGENTS.md: pin versions).
FROM ghcr.io/astral-sh/uv:0.13.7 AS uv

FROM python:3.12.16-slim-trixie
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1 UV_COMPILE_BYTECODE=1 UV_LINK_MODE=copy \
    UV_PROJECT_ENVIRONMENT=/opt/venv PATH=/opt/venv/bin:$PATH
COPY --from=uv /uv /usr/local/bin/uv
WORKDIR /srv
COPY pyproject.toml uv.lock ./
RUN uv sync --frozen --no-dev --no-install-project && rm /usr/local/bin/uv
COPY alembic.ini ./
COPY migrations ./migrations
COPY app ./app
RUN useradd --system --uid 10001 --no-create-home lumen
USER lumen
EXPOSE 8800 8801
HEALTHCHECK --interval=15s --timeout=3s --retries=3 \
  CMD python -c "import urllib.request,sys; sys.exit(0 if urllib.request.urlopen('http://127.0.0.1:8800/healthz',timeout=2).status==200 else 1)"
CMD ["python", "-m", "app.serve"]
