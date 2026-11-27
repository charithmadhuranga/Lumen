# Lumen Cloud developer commands (AGENTS.md §3).
.PHONY: setup up down logs migrate test lint fmt certs

setup:            ## install the locked environment
	uv sync --frozen

.env:
	cp .env.example .env

certs: dev-certs/server.pem
dev-certs/server.pem:
	tools/dev_certs.sh

up: .env certs    ## start postgres, redis, emqx and the api
	docker compose up -d --build --wait

down:
	docker compose down

logs:
	docker compose logs -f --tail=100

migrate:          ## apply migrations to the dev database (stack must be up)
	uv run alembic upgrade head

test:             ## needs postgres + redis from `make up`
	uv run pytest -q

lint:
	uv run ruff check . && uv run ruff format --check . && uv run mypy

fmt:
	uv run ruff format . && uv run ruff check --fix .
