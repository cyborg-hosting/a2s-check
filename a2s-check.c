#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdint.h>
//#include <unistd.h>
#include "ssq/a2s.h"
#include "docker.h"

unsigned int sleep(unsigned int seconds);

DOCKER *docker = NULL;
SSQ_QUERIER *querier = NULL;

void sigterm_handler(int sig)
{
    freeAndExit();
}

void freeAndExit()
{
    if(docker != NULL)
        docker_destroy(docker);

    if(querier != NULL)
        ssq_free(querier);
    
    exit(EXIT_FAILURE);
}

int main()
{
    docker = docker_init("v1.25");
    if(docker == NULL)
    {
        fputs(stderr, "docker environment not found (docker.sock volume may not exist)");
        freeAndExit();
    }
    CURLcode response;

    char buffer[256];

    querier = ssq_init();
    if(querier == NULL)
    {
        fputs(stderr, "Source server querier initialization failed");
        freeAndExit();
    }

    signal(SIGTERM, sigterm_handler);

    char *sContainerName = getenv("SRCDS_CONTAINER_NAME");
    if(sContainerName == NULL)
    {
        fputs(stderr, "environment variable 'SRCDS_CONTAINER_NAME' is not set.");
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
        fputs(stderr, "environment variable 'SRCDS_CONTAINER_NAME' is invalid (specified container does not exist).");
        freeAndExit();
    }

    char *sHost = getenv("SRCDS_HOST");
    if(sHost == NULL)
    {
        fputs(stderr, "environment variable 'SRCDS_HOST' is not set.");
        freeAndExit();
    }
    else
    {
        printf("Target SRCDS Host: %s\n", sHost);
    }

    char *sPort = getenv("SRCDS_PORT");
    if(sPort == NULL)
    {
        fputs(stderr, "environment variable 'SRCDS_PORT' is not set.");
        freeAndExit();
    }
    else if(sscanf(sPort, "%*hu") == EOF)
    {
        fputs(stderr, "environment variable 'SRCDS_PORT' is invalid (not a number)");
        freeAndExit();
    }
    else
    {
        printf("Target SRCDS Host: %s\n", sPort);
    }
    uint16_t iPort;
    sscanf(sPort, "%hu", &iPort);

    char *sInitTime = getenv("INITIAL_WAITING_TIME");
    int iInitTime = 60;
    if(sInitTime == NULL && sscanf(sInitTime, "%*d") != EOF)
    {
        sscanf(sInitTime, "%d", &iInitTime);
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

    int iErrorCount = 0;

    sprintf(buffer, "http://v1.25/containers/%s/restart", sContainerName);

    while(1)
    {
        A2S_INFO *info = ssq_info(querier);

        if(!ssq_ok(querier))
        {
            const char *error_message = ssq_errm(querier);
            fprintf(stderr, "ssq_info: %s\n", error_message);

            ssq_errclr(querier);

            iErrorCount += 1;
        }
        else
        {
            iErrorCount = 0;

            ssq_info_free(info);
        }

        if(iErrorCount == 5)
        {
            puts("Server Restarting...");
            iErrorCount = 0;

            response = docker_post(docker, buffer, "");
            if(response == CURLE_OK)
            {
                puts("Restart Sucess.");
            }
            else
            {
                fputs(stderr, "Restart Failed");
                freeAndExit();
            }

            sleep(iInitTime);
        }

        sleep(12);
    }
    
    docker_destroy(docker);
    ssq_free(querier);

    return 0;
}
