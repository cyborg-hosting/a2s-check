#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdint.h>
//#include <unistd.h>
#include "ssq/a2s.h"
#include "docker.h"

unsigned unsigned int sleep(unsigned int seconds);

DOCKER *docker = NULL;
SSQ_QUERIER *querier = NULL;
A2S_INFO *info = NULL;

void freeAndExit()
{
    if(docker != NULL)
        docker_destroy(docker);

    if(info != NULL)
        ssq_info_free(info);

    if(querier != NULL)
        ssq_free(querier);
    
    exit(EXIT_FAILURE);
}

void sigterm_handler(int sig)
{
    freeAndExit();
}

int main()
{
    docker = docker_init("v1.25");
    if(docker == NULL)
    {
        fputs("docker environment not found (docker.sock volume may not exist)\n", stderr);
        freeAndExit();
    }
    CURLcode response;

    char buffer[256];

    querier = ssq_init();
    if(querier == NULL)
    {
        fputs("Source server querier initialization failed\n", stderr);
        freeAndExit();
    }

    signal(SIGINT, sigterm_handler);

    char *sContainerName = getenv("SRCDS_CONTAINER_NAME");
    if(sContainerName == NULL)
    {
        fputs("environment variable 'SRCDS_CONTAINER_NAME' is not set.\n", stderr);
        freeAndExit();
    }
    sprintf(buffer, "http://v1.25/containers/%s/json", sContainerName);
    response = docker_get(docker, "buffer");
    if(response == CURLE_OK)
    {
        printf("Target Container Name: %s\n", sContainerName);
    }
    else
    {
        fputs("environment variable 'SRCDS_CONTAINER_NAME' is invalid (specified container does not exist).\n", stderr);
        freeAndExit();
    }

    char *sHost = getenv("SRCDS_HOST");
    if(sHost == NULL)
    {
        fputs("environment variable 'SRCDS_HOST' is not set.\n", stderr);
        freeAndExit();
    }
    else
    {
        printf("Target SRCDS Host: %s\n", sHost);
    }

    const char *sPort = getenv("SRCDS_PORT");
    if(sPort == NULL)
    {
        fputs("environment variable 'SRCDS_PORT' is not set.\n", stderr);
        freeAndExit();
    }
    else if(sscanf(sPort, "%*hu") == EOF)
    {
        fputs("environment variable 'SRCDS_PORT' is invalid (not a number)\n", stderr);
        freeAndExit();
    }
    else
    {
        printf("Target SRCDS Port: %s\n", sPort);
    }
    sPort = getenv("SRCDS_PORT");
    uint16_t iPort;
    sscanf(sPort, "%hu", &iPort);

    char *sInitTime = getenv("INITIAL_WAITING_TIME");
    int iInitTime = 60;
    if(sInitTime != NULL && sscanf(sInitTime, "%*d") != EOF)
    {
        sscanf(sInitTime, "%d", &iInitTime);
    }

    char *sWaitTime = getenv("WAITING_TIME");
    int iWaitTime = 60;
    if(sWaitTime != NULL && sscanf(sWaitTime, "%*d") != EOF)
    {
        sscanf(sWaitTime, "%d", &iWaitTime);
    }

    char *sPollingInterval = getenv("POLLING_INTERVAL");
    int iPollingInterval = 10;
    if(sPollingInterval != NULL && sscanf(sPollingInterval, "%*d") != EOF)
    {
        sscanf(sPollingInterval, "%d", &iPollingInterval);
    }

    ssq_set_target(querier, sHost, iPort);

    if (!ssq_ok(querier)) {
        const char *error_message = ssq_errm(querier);
        fprintf(stderr, "ssq_set_target: %s: %s\n", sHost, error_message);

        ssq_errclr(querier);

        freeAndExit();
    }

    printf("Wait for %d seconds to ensure the server is available on start\n", iInitTime);
    sleep(iInitTime);

    int iErrorTime = 0;

    sprintf(buffer, "http://v1.25/containers/%s/restart", sContainerName);

    while(1)
    {
        info = ssq_info(querier);

        if(!ssq_ok(querier))
        {
            const char *error_message = ssq_errm(querier);
            fprintf(stderr, "ssq_info: %s\n", error_message);

            ssq_errclr(querier);

            iErrorTime += iPollingInterval;

            printf("Error Time in Seconds: %d\n", iErrorTime);
        }
        else
        {
            if(iErrorTime != 0)
            {
                puts("Error Time in Seconds: 0");
            }

            iErrorTime = 0;

            ssq_info_free(info);
            info = NULL;
        }

        if(iErrorTime >= iWaitTime)
        {
            puts("Server Restarting...");
            iErrorTime = 0;

            response = docker_post(docker, buffer, "");
            if(response == CURLE_OK)
            {
                puts("Restart Success.");
            }
            else
            {
                fputs("Restart Failure.\n", stderr);
                freeAndExit();
            }

            sleep(iInitTime);
        }

        sleep(iPollingInterval);
    }
    
    if(docker != NULL)
        docker_destroy(docker);

    if(querier != NULL)
        ssq_free(querier);

    if(info != NULL)
        ssq_info_free(info);

    return 0;
}
