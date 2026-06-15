import { GsNumber } from "../shared/GsNumber";
import { Panel } from "../shared/Panel";
import type { AppConfig } from "../../types";

interface Props {
  form: AppConfig;
  onChange: (mutator: (draft: AppConfig) => void) => void;
}

export function GameSettingsTab({ form, onChange }: Props) {
  const cfg = form.gameServerSettings;
  return (
    <div className="@container grid max-w-2xl gap-3">
      <Panel title="Protocol (Engine.IO)">
        <div className="grid grid-cols-1 gap-3 @md:grid-cols-2 @xl:grid-cols-3">
          <GsNumber label="Ping interval (ms)" cfg={cfg} k="pingIntervalMs" set={onChange} />
          <GsNumber label="Ping timeout (ms)" cfg={cfg} k="pingTimeoutMs" set={onChange} />
          <GsNumber label="Max payload (bytes)" cfg={cfg} k="maxPayloadBytes" set={onChange} />
        </div>
      </Panel>
      <Panel title="Rate limits">
        <div className="grid grid-cols-1 gap-3 @md:grid-cols-2 @xl:grid-cols-3">
          <GsNumber label="Messages / sec" cfg={cfg} k="messageRatePerSec" set={onChange} />
          <GsNumber label="Conns / IP" cfg={cfg} k="ipConnectionLimit" set={onChange} />
          <GsNumber
            label="New conns / sec / IP"
            cfg={cfg}
            k="ipConnectionRatePerSec"
            set={onChange}
          />
        </div>
      </Panel>
      <Panel title="Accounts">
        <div className="grid grid-cols-1 gap-3 @md:grid-cols-2 @xl:grid-cols-3">
          <GsNumber label="Create / day / IP" cfg={cfg} k="accountCreatePerDay" set={onChange} />
          <GsNumber label="Create / hour / IP" cfg={cfg} k="accountCreatePerHour" set={onChange} />
          <GsNumber label="Login pace (ms)" cfg={cfg} k="loginPaceMs" set={onChange} />
          <GsNumber label="Login queue notice" cfg={cfg} k="loginQueueThreshold" set={onChange} />
          <GsNumber label="PBKDF2 iterations" cfg={cfg} k="pbkdf2Iterations" set={onChange} />
          <GsNumber
            label="Reset throttle (ms)"
            cfg={cfg}
            k="passwordResetThrottleMs"
            set={onChange}
          />
          <GsNumber
            label="Relationship delay (ms)"
            cfg={cfg}
            k="relationshipDelayMs"
            set={onChange}
          />
        </div>
      </Panel>
      <Panel title="Rooms & text limits">
        <div className="grid grid-cols-1 gap-3 @md:grid-cols-2 @xl:grid-cols-3">
          <GsNumber label="Search results cap" cfg={cfg} k="searchMaxResults" set={onChange} />
          <GsNumber label="Room default size" cfg={cfg} k="roomLimitDefault" set={onChange} />
          <GsNumber label="Room min size" cfg={cfg} k="roomLimitMin" set={onChange} />
          <GsNumber label="Room max size" cfg={cfg} k="roomLimitMax" set={onChange} />
          <GsNumber label="Description max len" cfg={cfg} k="descriptionMaxLen" set={onChange} />
          <GsNumber label="Email max len" cfg={cfg} k="emailMaxLen" set={onChange} />
          <GsNumber label="Name max len" cfg={cfg} k="nameMaxLen" set={onChange} />
          <GsNumber
            label="Ownership notes max len"
            cfg={cfg}
            k="ownershipNotesMaxLen"
            set={onChange}
          />
        </div>
      </Panel>
      <Panel title="Timers">
        <div className="grid grid-cols-1 gap-3 @md:grid-cols-2 @xl:grid-cols-3">
          <GsNumber
            label="ServerInfo interval (s)"
            cfg={cfg}
            k="serverInfoIntervalSec"
            set={onChange}
          />
          <GsNumber
            label="Delayed flush (s)"
            cfg={cfg}
            k="delayedFlushIntervalSec"
            set={onChange}
          />
        </div>
        <p className="mt-2.5 text-xs text-muted-foreground">
          All values apply live: in-flight connections keep running and pick up the new limits on
          their next event, tick or password hash. PBKDF2 salt/hash sizes are fixed; the iteration
          count is stored per hash, so old logins keep working.
        </p>
      </Panel>
    </div>
  );
}
