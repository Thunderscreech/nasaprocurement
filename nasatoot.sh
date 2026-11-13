#!/bin/bash

# Define variables
# MASTODON_TOKEN and MASTODON_INSTANCE come from config.ini next to this script (not in git)
CONFIG="${NASAPROC_CONFIG:-$(dirname "$(readlink -f "$0")")/config.ini}"
cfg() { sed -n "s/^$1[[:space:]]*=[[:space:]]*\"\{0,1\}\([^\"]*\)\"\{0,1\}[[:space:]]*$/\1/p" "$CONFIG" | head -n 1; }
ACCESS_TOKEN="$(cfg MASTODON_TOKEN)"
INSTANCE_URL="${NASATOOT_INSTANCE_URL:-$(cfg MASTODON_INSTANCE)}"  # env override is for testing
if [ -z "$ACCESS_TOKEN" ] || [ -z "$INSTANCE_URL" ]; then
    echo "MASTODON_TOKEN / MASTODON_INSTANCE missing from $CONFIG" >&2
    exit 1
fi
POST_TEXT="$1"  # The text of the toot
IMG_PATHS=("${@:2:4}")  # Accept up to 4 image paths as additional arguments

# Array to hold media IDs
MEDIA_IDS=()

# Step 1: Upload each media (if images are provided)
IMG_DIR="$(dirname "$(readlink -f "$0")")/rendered_opportunities"
for IMG_PATH in "${IMG_PATHS[@]}"; do
    [[ -z "$IMG_PATH" ]] && continue
    # Bare file names are relative to the render folder (cron doesn't run from here)
    [[ "$IMG_PATH" != /* ]] && IMG_PATH="$IMG_DIR/$IMG_PATH"
    if [[ ! -r "$IMG_PATH" ]]; then
        echo "Skipping missing/unreadable image: $IMG_PATH"
        continue
    fi

    # Try each upload a few times; a failed image is skipped, not fatal
    MEDIA_ID=""
    for ATTEMPT in 1 2 3; do
        echo "Uploading media (attempt $ATTEMPT): $IMG_PATH"
        MEDIA_RESPONSE=$(curl -s --connect-timeout 15 --max-time 120 -w "\n%{http_code}" -X POST "$INSTANCE_URL/api/v2/media" \
            -H "Authorization: Bearer $ACCESS_TOKEN" \
            -F "file=@$IMG_PATH")
        MEDIA_JSON=$(echo "$MEDIA_RESPONSE" | sed '$d')
        MEDIA_STATUS=$(echo "$MEDIA_RESPONSE" | tail -n1)

        # 200 = processed, 202 = accepted and still processing (normal for v2 media)
        if [[ "$MEDIA_STATUS" == "200" || "$MEDIA_STATUS" == "202" ]]; then
            MEDIA_ID=$(echo "$MEDIA_JSON" | jq -r '.id // empty')
            [[ -n "$MEDIA_ID" ]] && break
        fi
        echo "Media upload failed with status $MEDIA_STATUS: $MEDIA_JSON"
        # Don't retry client errors other than rate limiting
        if [[ "$MEDIA_STATUS" =~ ^4 && "$MEDIA_STATUS" != "429" ]]; then break; fi
        sleep $((ATTEMPT * 3))
    done

    if [[ -n "$MEDIA_ID" ]]; then
        MEDIA_IDS+=("$MEDIA_ID")
        echo "Media uploaded successfully. Media ID: $MEDIA_ID"
    else
        echo "Giving up on $IMG_PATH; posting without it."
    fi
done

# Wait (up to ~30s each) for any still-processing media so the status post doesn't reject them
for MEDIA_ID in "${MEDIA_IDS[@]}"; do
    for i in $(seq 1 10); do
        CODE=$(curl -s --connect-timeout 10 --max-time 20 -o /dev/null -w "%{http_code}" "$INSTANCE_URL/api/v1/media/$MEDIA_ID" -H "Authorization: Bearer $ACCESS_TOKEN")
        [[ "$CODE" == "200" ]] && break
        sleep 3
    done
done

# Step 2: Post the status
echo "Posting status: $POST_TEXT"

# Create JSON data using jq to ensure proper escaping
if [[ ${#MEDIA_IDS[@]} -gt 0 ]]; then
    POST_DATA=$(jq -n --arg status "$POST_TEXT" --argjson media_ids "$(printf '%s\n' "${MEDIA_IDS[@]}" | jq -R . | jq -s .)" \
        '{status: $status, media_ids: $media_ids}')
else
    POST_DATA=$(jq -n --arg status "$POST_TEXT" '{status: $status}')
fi

# Same text -> same key.  If a post times out after the server accepted it, the bot's retry next run
# (same notice, same text) gets the original status back instead of creating a duplicate toot.
IDEMPOTENCY_KEY="nasaprocurement-$(printf '%s' "$POST_TEXT" | sha256sum | cut -c1-40)"

# POST a status; sets TOOT_JSON and TOOT_STATUS ("000" = no answer: connect failure or timeout)
post_status() {
    local RESPONSE
    RESPONSE=$(curl -s --connect-timeout 15 --max-time 60 -w "\n%{http_code}" -X POST "$INSTANCE_URL/api/v1/statuses" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json" \
        -H "Idempotency-Key: $IDEMPOTENCY_KEY" \
        -d "$1")
    TOOT_JSON=$(echo "$RESPONSE" | sed '$d')
    TOOT_STATUS=$(echo "$RESPONSE" | tail -n1)
}

# Send the toot
post_status "$POST_DATA"

# If the server rejected the post with media, fall back to text only.  Not on "000": a timeout may
# mean the post went through, and a text-only retry would then be a second toot.
if [[ "$TOOT_STATUS" != "200" && "$TOOT_STATUS" != "000" && ${#MEDIA_IDS[@]} -gt 0 ]]; then
    echo "Status post with media failed ($TOOT_STATUS: $TOOT_JSON); retrying as text only"
    post_status "$(jq -n --arg status "$POST_TEXT" '{status: $status}')"
fi

# Check if the status post was successful
if [[ "$TOOT_STATUS" == "000" ]]; then
    echo "Status post got no response (connection failed or timed out); the bot will retry next run"
    exit 1
elif [[ "$TOOT_STATUS" != "200" ]]; then
    echo "Status post failed with status $TOOT_STATUS"
    echo "Response: $TOOT_JSON"
    exit 1
else
    echo "Status posted successfully!"
    echo "Response: $TOOT_JSON"
fi
