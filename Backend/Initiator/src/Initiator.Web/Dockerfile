# The build context MUST be the solution root (Backend/Initiator), not the
# repository root and not the web project directory:
#
#   cd Backend/Initiator
#   docker build -f src/Initiator.Web/Dockerfile -t initiator-web .
#
# On Render that means setting the service's Root Directory to
# Backend/Initiator. With the wrong context every COPY below fails with
# "/src": not found, and nothing in the message says why.
#
# The test project is deliberately absent. The image needs Domain, DataAccess
# and Web and nothing else, and referencing tests/ here would make the build
# depend on a directory that is not always in the context.

FROM mcr.microsoft.com/dotnet/sdk:9.0 AS build
WORKDIR /src

# Project files first, on their own layer: restore is the slow step and it only
# has to run again when a dependency actually changes, not on every code edit.
COPY src/Initiator.Domain/Initiator.Domain.csproj src/Initiator.Domain/
COPY src/Initiator.DataAccess/Initiator.DataAccess.csproj src/Initiator.DataAccess/
COPY src/Initiator.Web/Initiator.Web.csproj src/Initiator.Web/
RUN dotnet restore src/Initiator.Web/Initiator.Web.csproj

COPY src/ src/
RUN dotnet publish src/Initiator.Web/Initiator.Web.csproj \
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
ENTRYPOINT ["/bin/sh", "-c", "exec dotnet Initiator.Web.dll --urls http://+:${PORT:-8080}"]
