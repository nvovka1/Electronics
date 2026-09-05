# Build context is the solution root:
#   docker build -f src/LoraFleet.Web/Dockerfile -t lorafleet-web .
#
# The test project is deliberately absent from this file. The image needs
# Domain, DataAccess and Web and nothing else, and referencing tests/ here made
# the build depend on a directory that is not always in the context.

FROM mcr.microsoft.com/dotnet/sdk:9.0 AS build
WORKDIR /src

# Project files first, on their own layer: restore is the slow step and it only
# has to run again when a dependency actually changes, not on every code edit.
COPY src/LoraFleet.Domain/LoraFleet.Domain.csproj src/LoraFleet.Domain/
COPY src/LoraFleet.DataAccess/LoraFleet.DataAccess.csproj src/LoraFleet.DataAccess/
COPY src/LoraFleet.Web/LoraFleet.Web.csproj src/LoraFleet.Web/
RUN dotnet restore src/LoraFleet.Web/LoraFleet.Web.csproj

COPY src/ src/
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

ENV ASPNETCORE_ENVIRONMENT=Production
ENV DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=false

# Documentation only. The port actually bound is whatever PORT says at run time.
EXPOSE 8080

COPY --from=build /app/publish .

# Render, Fly and Cloud Run all inject PORT and expect the process to bind it.
# Hard-coding 8080 makes the platform's health probe hit a port nothing is
# listening on, and the deploy then fails with nothing in the application log
# to explain why. exec, so dotnet stays PID 1 and receives SIGTERM directly.
ENTRYPOINT ["/bin/sh", "-c", "exec dotnet LoraFleet.Web.dll --urls http://+:${PORT:-8080}"]
