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


// eslint-disable-next-line no-duplicate-imports
import type { AuditEventData } from './AuditEventData';


/**
 * 
 * @export
 * @interface AuditEventResponse
 */
export interface AuditEventResponse  {
  /**
   * 
   * @type {AuditEventData}
   * @memberof AuditEventResponse
   */
  audit_event_data?: AuditEventData;
  /**
   * Audit Event detailed data
   * @type {{ [key: string]: object; }}
   * @memberof AuditEventResponse
   */
  audit_event_detailed_data?: { [key: string]: object; };
}



