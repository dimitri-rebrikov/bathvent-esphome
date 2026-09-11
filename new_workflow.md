```mermaid
flowchart TD
    start([Start]) --> read_current_sensor_value[fill SensorCurrentValue for every sensor, if NAN fill with ConfigSensorNanValue of corresponding Sensor]
    read_current_sensor_value --> fan_state_check{Fan State Check}

    fan_state_check -- run --> running_time_check{CurrentTime - LastSensorCheckTimestamp > ConfigSensorChangeCheckInterval ? }
    running_time_check -- yes --> sensor_change_check{"Is for any sensor: abs(SensorStoredValue - SensorCurrentValue) > ConfigSensorChangeThreshold (separate value for each sensor)"}
    running_time_check -- no --> fan_run[Fan Run]
    sensor_change_check -- no --> fan_off
    sensor_change_check -- yes --> fan_start_run
    fan_start_run --> update_stored_sensor_values[Update SensorStoredValue with SensorCurrentValue for every sensor]
    update_stored_sensor_values --> reset_last_sensor_check_timestamp[LastSensorCheckTimestamp = CurrentTime]
  
    fan_state_check -- off --> flush_presence_check{Is human present}
    flush_presence_check -- yes --> start_flush[Start Flush]
    flush_presence_check -- no --> time_for_control_run_check{CurrentTime - LastOnTimestamp > ConfigMaxOffTime}
    time_for_control_run_check -- yes --> start_flush
    time_for_control_run_check -- no --> fan_off

    fan_state_check -- sniff --> sensor_check{Check SensorCurrentValue for every sensor }
    sensor_check -- "any value is above its SensorThresholdValue" --> fan_start_run
    sensor_check -- "all values are below their respective SensorThresholdValue" --> sniff_presence_check{Is human present}
    sniff_presence_check -- yes --> reset_after_run_timestamp[AfterRunTimestamp = CurrentTime]
    reset_after_run_timestamp --> fan_sniff
    sniff_presence_check -- no --> after_run_finished_check{CurrentTime - AfterRunTimestamp > ConfigAfterRunDuration}
    after_run_finished_check -- yes --> fan_off
    after_run_finished_check -- no --> fan_sniff

    fan_state_check -- flush --> time_to_sniff_check{CurrentTime - FlushStartedTimestamp > ConfigFlushDuration}
    time_to_sniff_check -- yes --> fan_sniff[Sniff]
    time_to_sniff_check -- no --> keep_flush[Keep Flush]

    fan_start_run --> fan_run
    fan_run --> run_full_level_check{Is human absent}
    run_full_level_check -- yes --> run_fan_on_full[Run Fan on FULL level]
    run_full_level_check -- no --> run_fan_on_mid[Run Fan on MID level]

    start_flush --> reset_flush_started_timestamp[FlushStartedTimestamp = CurrentTime]
    start_flush --> run_fan_on_low[Run Fan on LOW level]
    keep_flush --> run_fan_on_low
    fan_sniff --> run_fan_on_low

    run_fan_on_low --> reset_last_on_timestamp[LastOnTimestamp = CurrentTime]
    run_fan_on_mid --> reset_last_on_timestamp[LastOnTimestamp = CurrentTime]
    run_fan_on_full --> reset_last_on_timestamp[LastOnTimestamp = CurrentTime]
```
Additional information:

Every step and decision of a loop shall be logged into log as long text and additionally incrementally logged as short text into one string which at the end will be posted to the mqtt 


