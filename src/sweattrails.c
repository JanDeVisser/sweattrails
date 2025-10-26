#include <stdio.h>
#include <string.h>

#include "../fitsdk/fit_convert.h"

#define SLICE_IMPLEMENTATION
#define DA_IMPLEMENTATION
#define IO_IMPLEMENTATION
#define ZORRO_IMPLEMENTATION
#include "da.h"
#include "io.h"
#include "zorro.h"

#define SCHEMA_IMPLEMENTATION
#include "schema.h"
#include "sweattrails.h"

#define RECORD_TABLE 0

bool drop_everything = false;

int read_fit_file(slice_t file_name)
{
    FIT_CONVERT_RETURN convert_return = FIT_CONVERT_CONTINUE;
    FIT_UINT32         mesg_index = 0;

    FitConvert_Init(FIT_TRUE);
    slice_t contents = sb_as_slice(MUSTOPT(sb_t, slurp_file(file_name)));
    while (convert_return == FIT_CONVERT_CONTINUE) {
        do {
            convert_return = FitConvert_Read(contents.items, contents.len);

            switch (convert_return) {
            case FIT_CONVERT_MESSAGE_AVAILABLE: {
                const FIT_UINT8 *mesg = FitConvert_GetMessageData();
                FIT_UINT16       mesg_num = FitConvert_GetMessageNumber();

                printf("Mesg %d (%d) - ", mesg_index++, mesg_num);

                switch (mesg_num) {
                case FIT_MESG_NUM_FILE_ID: {
                    const FIT_FILE_ID_MESG *id = (FIT_FILE_ID_MESG *) mesg;
                    printf("File ID: type=%u, number=%u\n", id->type, id->number);
                    break;
                }

                case FIT_MESG_NUM_USER_PROFILE: {
                    const FIT_USER_PROFILE_MESG *user_profile = (FIT_USER_PROFILE_MESG *) mesg;
                    printf("User Profile: weight=%0.1fkg\n", user_profile->weight / 10.0f);
                    break;
                }

                case FIT_MESG_NUM_ACTIVITY: {
                    const FIT_ACTIVITY_MESG *activity = (FIT_ACTIVITY_MESG *) mesg;
                    printf("Activity: timestamp=%u, type=%u, event=%u, event_type=%u, num_sessions=%u\n", activity->timestamp, activity->type, activity->event, activity->event_type, activity->num_sessions);
                    {
                        FIT_ACTIVITY_MESG old_mesg;
                        old_mesg.num_sessions = 1;
                        FitConvert_RestoreFields(&old_mesg);
                        printf("Restored num_sessions=1 - Activity: timestamp=%u, type=%u, event=%u, event_type=%u, num_sessions=%u\n", activity->timestamp, activity->type, activity->event, activity->event_type, activity->num_sessions);
                    }
                    break;
                }

                case FIT_MESG_NUM_SESSION: {
                    const FIT_SESSION_MESG *session = (FIT_SESSION_MESG *) mesg;
                    printf("Session: timestamp=%u\n", session->timestamp);
                    break;
                }

                case FIT_MESG_NUM_LAP: {
                    const FIT_LAP_MESG *lap = (FIT_LAP_MESG *) mesg;
                    printf("Lap: timestamp=%u\n", lap->timestamp);
                    break;
                }

                case FIT_MESG_NUM_RECORD: {
                    const FIT_RECORD_MESG *record = (FIT_RECORD_MESG *) mesg;

                    printf("Record: timestamp=%u", record->timestamp);

                    if (
                        (record->compressed_speed_distance[0] != FIT_BYTE_INVALID) || (record->compressed_speed_distance[1] != FIT_BYTE_INVALID) || (record->compressed_speed_distance[2] != FIT_BYTE_INVALID)) {
                        static FIT_UINT32 accumulated_distance16 = 0;
                        static FIT_UINT32 last_distance16 = 0;
                        FIT_UINT16        speed100;
                        FIT_UINT32        distance16;

                        speed100 = record->compressed_speed_distance[0] | ((record->compressed_speed_distance[1] & 0x0F) << 8);
                        printf(", speed = %0.2fm/s", speed100 / 100.0f);

                        distance16 = (record->compressed_speed_distance[1] >> 4) | (record->compressed_speed_distance[2] << 4);
                        accumulated_distance16 += (distance16 - last_distance16) & 0x0FFF;
                        last_distance16 = distance16;

                        printf(", distance = %0.3fm", accumulated_distance16 / 16.0f);
                    }

                    printf("\n");
                    break;
                }

                case FIT_MESG_NUM_EVENT: {
                    const FIT_EVENT_MESG *event = (FIT_EVENT_MESG *) mesg;
                    printf("Event: timestamp=%u\n", event->timestamp);
                    break;
                }

                case FIT_MESG_NUM_DEVICE_INFO: {
                    const FIT_DEVICE_INFO_MESG *device_info = (FIT_DEVICE_INFO_MESG *) mesg;
                    printf("Device Info: timestamp=%u\n", device_info->timestamp);
                    break;
                }

                default:
                    printf("Unknown\n");
                    break;
                }
                break;
            }

            default:
                break;
            }
        } while (convert_return == FIT_CONVERT_MESSAGE_AVAILABLE);
    }

    if (convert_return == FIT_CONVERT_ERROR) {
        printf("Error decoding file.\n");
        return 1;
    }

    if (convert_return == FIT_CONVERT_CONTINUE) {
        printf("Unexpected end of file.\n");
        return 1;
    }

    if (convert_return == FIT_CONVERT_DATA_TYPE_NOT_SUPPORTED) {
        printf("File is not FIT.\n");
        return 1;
    }

    if (convert_return == FIT_CONVERT_PROTOCOL_VERSION_NOT_SUPPORTED) {
        printf("Protocol version not supported.\n");
        return 1;
    }

    if (convert_return == FIT_CONVERT_END_OF_FILE)
        printf("File converted successfully.\n");

    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: sweattrails <fit file>\n");
        return 1;
    }
    db_t db = db_make(
        C("sweattrails"),
        C("sweattrails"),
        C(""),
        C("localhost"),
        5432);
    table_defs_t schema = sweattrails_init_schema(&db);
    (void) schema;
    read_fit_file(C(argv[1]));
    return 0;
}
