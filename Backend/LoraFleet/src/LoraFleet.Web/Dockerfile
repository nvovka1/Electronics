# Build context is the solution root, so the restore can see every project.
#   docker build -f src/LoraFleet.Web/Dockerfile .

FROM mcr.microsoft.com/dotnet/sdk:9.0 AS build
WORKDIR /src

# Project files first, on their own layer: restore is the slow step and it only
# has to run again when a dependency actually changes, not on every code edit.
COPY LoraFleet.sln .
COPY src/LoraFleet.Domain/LoraFleet.Domain.csproj src/LoraFleet.Domain/
COPY src/LoraFleet.DataAccess/LoraFleet.DataAccess.csproj src/LoraFleet.DataAccess/
COPY src/LoraFleet.Web/LoraFleet.Web.csproj src/LoraFleet.Web/
COPY tests/LoraFleet.Tests/LoraFleet.Tests.csproj tests/LoraFleet.Tests/
RUN dotnet restore src/LoraFleet.Web/LoraFleet.Web.csproj

COPY . .
RUN dotnet publish src/LoraFleet.Web/LoraFleet.Web.csproj \
        -c Release \
        -o /app/publish \
        --no-restore \
        /p:UseAppHost=false

FROM mcr.microsoft.com/dotnet/aspnet:9.0 AS runtime
WORKDIR /app

# Not root. A container that only serves HTTP has no business being able to
# write to its own filesystem.
RUN adduser --system --uid 5678 --group appuser
USER appuser

# Plain HTTP on 8080: TLS belongs to whatever sits in front of this, and a
# self-signed certificate inside the container only makes devices fail to
# connect for a reason nobody can see.
ENV ASPNETCORE_URLS=http://+:8080 \
    ASPNETCORE_ENVIRONMENT=Production \
    DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=false

EXPOSE 8080

COPY --from=build /app/publish .

# The health endpoint reports the process, not Mongo. A database outage should
# not make the orchestrator kill the one thing able to report the outage.
HEALTHCHECK --interval=30s --timeout=3s --start-period=10s --retries=3 \
    CMD ["/bin/sh", "-c", "wget -qO- http://127.0.0.1:8080/healthz || exit 1"]

ENTRYPOINT ["dotnet", "LoraFleet.Web.dll"]
