// tslint:disable
/**
 * Yugabyte Cloud
 * YugabyteDB as a Service
 *
 * The version of the OpenAPI document: v1
 * Contact: support@yugabyte.com
 *
 * NOTE: This class is auto generated by OpenAPI Generator (https://openapi-generator.tech).
 * https://openapi-generator.tech
 * Do not edit the class manually.
 */




/**
 * Schema for Live Query Response YCQL Query Item
 * @export
 * @interface LiveQueryResponseYCQLQueryItem
 */
export interface LiveQueryResponseYCQLQueryItem  {
  /**
   * 
   * @type {string}
   * @memberof LiveQueryResponseYCQLQueryItem
   */
  id?: string;
  /**
   * 
   * @type {string}
   * @memberof LiveQueryResponseYCQLQueryItem
   */
  node_name?: string;
  /**
   * 
   * @type {string}
   * @memberof LiveQueryResponseYCQLQueryItem
   */
  keyspace?: string;
  /**
   * 
   * @type {string}
   * @memberof LiveQueryResponseYCQLQueryItem
   */
  query?: string;
  /**
   * 
   * @type {string}
   * @memberof LiveQueryResponseYCQLQueryItem
   */
  type?: string;
  /**
   * 
   * @type {number}
   * @memberof LiveQueryResponseYCQLQueryItem
   */
  elapsed_millis?: number;
  /**
   * 
   * @type {string}
   * @memberof LiveQueryResponseYCQLQueryItem
   */
  client_host?: string;
  /**
   * 
   * @type {string}
   * @memberof LiveQueryResponseYCQLQueryItem
   */
  client_port?: string;
}



