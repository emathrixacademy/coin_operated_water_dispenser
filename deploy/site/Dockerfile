# Railway build of the project page from the repository root (see /railway.json).
# Only the page and the client documents named in server.py go into the image.
FROM python:3.11-slim
WORKDIR /app/deploy/site
COPY deploy/site/requirements.txt ./requirements.txt
RUN pip install --no-cache-dir -r requirements.txt
COPY deploy/site/server.py ./server.py
COPY deploy/site/public ./public
COPY docs/SPECIFICATION.md docs/scenarios.md docs/calibration.md docs/wiring.md docs/wiring-asbuilt.html docs/Water-Vendo-Wiring.pdf docs/Emathrix_FabDrawing_CoinRouting_RevB.pdf docs/WATER-VENDO.pdf /app/docs/
ENV PYTHONUNBUFFERED=1 DOCS_DIR=/app/docs
CMD ["python", "server.py"]
