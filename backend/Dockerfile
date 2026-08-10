FROM python:3.12-slim

WORKDIR /srv
COPY pyproject.toml ./
COPY app ./app
COPY alembic ./alembic
COPY alembic.ini ./

RUN pip install --no-cache-dir .

# Drop root: an app-level RCE should not land as root inside the container.
RUN useradd --system --uid 10001 appuser && chown -R appuser /srv
USER appuser

EXPOSE 8000
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
