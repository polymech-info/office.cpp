# Scraper Request

## OpenAPI Specification

```yaml
openapi: 3.0.1
info:
  title: ''
  description: ''
  version: 1.0.0
paths:
  /api/v1/scraper/request:
    post:
      summary: Scraper Request
      deprecated: false
      description: ''
      tags:
        - Scraping API
      parameters: []
      requestBody:
        content:
          application/json:
            schema:
              type: object
              properties:
                actor:
                  type: string
                input:
                  type: object
                  properties:
                    url:
                      type: string
                  required:
                    - url
                  x-apidog-orders:
                    - url
                proxy:
                  type: object
                  properties:
                    country:
                      type: string
                  required:
                    - country
                  x-apidog-orders:
                    - country
                async:
                  type: boolean
                  description: |-
                    If true, the task will be executed asynchronously. 
                    If false, the task will be executed synchronously.
              required:
                - actor
                - input
                - proxy
              x-apidog-orders:
                - actor
                - input
                - proxy
                - async
            example:
              actor: scraper.xxx
              input:
                url: >-
                  https://www.***.com/shop/us/products/stmicroelectronics/tda7265a-3074457345625542393/
              proxy:
                country: US
              async: false
      responses:
        '200':
          description: ''
          content:
            application/json:
              schema:
                type: object
                properties: {}
                x-apidog-orders: []
          headers: {}
          x-apidog-name: Success
      security:
        - apikey-header-x-api-token: []
      x-apidog-folder: Scraping API
      x-apidog-status: released
      x-run-in-apidog: https://app.apidog.com/web/project/745098/apis/api-11949852-run
components:
  schemas: {}
  securitySchemes:
    bearer:
      type: bearer
      scheme: bearer
      description: Bearer token authentication using your Scrapeless API key
    apikey-header-x-api-token:
      type: apiKey
      in: header
      name: x-api-token
servers:
  - url: https://api.scrapeless.com
    description: Prod Env
security:
  - apikey-header-x-api-token: []

```