# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Shared data types used across the wrapper."""

from __future__ import absolute_import


class BundlePrice(object):
    """A credit bundle with pricing.

    Attributes:
        credits: Number of credits in this bundle.
        price: Effective price (sale or regular) as a decimal string.
        regular_price: Non-discounted price as a decimal string.
        sale_price: Discounted price as a decimal string, empty if no sale.
    """

    __slots__ = ("credits", "price", "regular_price", "sale_price")

    def __init__(self, credits=0, price="", regular_price="", sale_price=""):
        # type: (int, str, str, str) -> None
        self.credits = credits
        self.price = price
        self.regular_price = regular_price
        self.sale_price = sale_price

    def __repr__(self):
        # type: () -> str
        return "BundlePrice(credits={0!r}, price={1!r})".format(
            self.credits, self.price
        )


class PricingInfo(object):
    """Pricing configuration for the machine.

    Attributes:
        free_play: Whether the machine is in free play mode.
        payments_enabled: Whether payments are enabled.
        credit_price: Effective price per credit as a decimal string.
        credit_regular_price: Non-discounted price per credit.
        credit_sale_price: Sale price per credit, empty if no sale.
        bundles: List of :class:`BundlePrice` objects.
    """

    __slots__ = (
        "free_play",
        "payments_enabled",
        "credit_price",
        "credit_regular_price",
        "credit_sale_price",
        "bundles",
    )

    def __init__(
        self,
        free_play=False,
        payments_enabled=False,
        credit_price="",
        credit_regular_price="",
        credit_sale_price="",
        bundles=None,
    ):
        # type: (bool, bool, str, str, str, list) -> None
        self.free_play = free_play
        self.payments_enabled = payments_enabled
        self.credit_price = credit_price
        self.credit_regular_price = credit_regular_price
        self.credit_sale_price = credit_sale_price
        self.bundles = bundles if bundles is not None else []

    def __repr__(self):
        # type: () -> str
        return "PricingInfo(free_play={0!r}, payments_enabled={1!r}, credit_price={2!r}, bundles={3!r})".format(
            self.free_play, self.payments_enabled, self.credit_price, len(self.bundles)
        )


class PlayerInfo(object):
    """Player profile information.

    Attributes:
        id: The player's id in the Scorbit system.
        preferred_name: The player's preferred display name.
        name: The player's full name.
        initials: The player's initials.
        picture_url: URL to the player's profile picture.
        claim_deeplink: Claim URL for unclaimed player slots (empty if claimed).
    """

    __slots__ = (
        "id",
        "preferred_name",
        "name",
        "initials",
        "picture_url",
        "claim_deeplink",
        "_has_info",
    )

    def __init__(
        self,
        id="",
        preferred_name="",
        name="",
        initials="",
        picture_url="",
        claim_deeplink="",
        has_info=False,
    ):
        # type: (str, str, str, str, str, str, bool) -> None
        self.id = id
        self.preferred_name = preferred_name
        self.name = name
        self.initials = initials
        self.picture_url = picture_url
        self.claim_deeplink = claim_deeplink
        self._has_info = has_info

    def has_info(self):
        # type: () -> bool
        """Return True if the player slot is claimed and has profile info.

        When False, only :attr:`claim_deeplink` is meaningful.
        """
        return self._has_info

    def __repr__(self):
        # type: () -> str
        if self._has_info:
            return "PlayerInfo(id={0!r}, preferred_name={1!r})".format(
                self.id, self.preferred_name
            )
        return "PlayerInfo(claim_deeplink={0!r})".format(self.claim_deeplink)


class AchievementUnlock(object):
    """An achievement Scorbit granted a claimed player (``AchievementUnlocked``).

    Attributes:
        key: The achievement's key.
        player: The player number it was granted to.
        name: The achievement's name.
        description: Its description.
        artwork: The artwork chosen with
            :meth:`Config.set_achievement_artwork`, or ``b""`` when there is none.
    """

    __slots__ = ("key", "player", "name", "description", "artwork")

    def __init__(self, key="", player=0, name="", description="", artwork=b""):
        # type: (str, int, str, str, bytes) -> None
        self.key = key
        self.player = player
        self.name = name
        self.description = description
        self.artwork = artwork

    def __repr__(self):
        # type: () -> str
        return "AchievementUnlock(key={!r}, player={!r})".format(self.key, self.player)


class CompetitionGoal(object):
    """A competition goal the SDK installed or refused (``CompetitionGoalReceived``).

    The SDK installs the goal and starts the attempt itself; the event only
    says what happened.

    Attributes:
        installed: Whether the goal was installed.
        attempt: The attempt token.
        player: The admitted player's user uuid.
        position: Where Scorbit expects the player, or 0 when it gave none.
        reason: Why it was refused; empty when installed.
    """

    __slots__ = ("installed", "attempt", "player", "position", "reason")

    def __init__(self, installed=False, attempt="", player="", position=0, reason=""):
        # type: (bool, str, str, int, str) -> None
        self.installed = installed
        self.attempt = attempt
        self.player = player
        self.position = position
        self.reason = reason

    def __repr__(self):
        # type: () -> str
        return "CompetitionGoal(installed={!r}, attempt={!r})".format(
            self.installed, self.attempt
        )


class LeaderboardPlayer(object):
    """Leaderboard player information."""

    __slots__ = (
        "id",
        "username",
        "display_name",
        "initials",
        "avatar",
        "follower_count",
        "following_count",
        "last_login",
    )

    def __init__(
        self,
        id="",
        username="",
        display_name="",
        initials="",
        avatar="",
        follower_count=0,
        following_count=0,
        last_login="",
    ):
        # type: (str, str, str, str, str, int, int, str) -> None
        self.id = id
        self.username = username
        self.display_name = display_name
        self.initials = initials
        self.avatar = avatar
        self.follower_count = follower_count
        self.following_count = following_count
        self.last_login = last_login

    def __repr__(self):
        # type: () -> str
        return "LeaderboardPlayer(id={0!r}, username={1!r})".format(
            self.id, self.username
        )


class LeaderboardEntry(object):
    """Single leaderboard entry."""

    __slots__ = (
        "id",
        "rank",
        "player",
        "high_score",
        "image",
        "reaction_count",
        "score_count",
        "is_nfc_verified",
        "is_verified",
        "is_vpin",
        "created",
    )

    def __init__(
        self,
        id=0,
        rank=0,
        player=None,
        high_score=0,
        image="",
        reaction_count=0,
        score_count=0,
        is_nfc_verified=False,
        is_verified=False,
        is_vpin=False,
        created="",
    ):
        # type: (int, int, object, int, str, int, int, bool, bool, bool, str) -> None
        self.id = id
        self.rank = rank
        self.player = player if player is not None else LeaderboardPlayer()
        self.high_score = high_score
        self.image = image
        self.reaction_count = reaction_count
        self.score_count = score_count
        self.is_nfc_verified = is_nfc_verified
        self.is_verified = is_verified
        self.is_vpin = is_vpin
        self.created = created

    def __repr__(self):
        # type: () -> str
        return "LeaderboardEntry(id={0!r}, rank={1!r}, high_score={2!r})".format(
            self.id, self.rank, self.high_score
        )


class LeaderboardResult(object):
    """Typed leaderboard response."""

    __slots__ = ("entries",)

    def __init__(self, entries=None):
        # type: (object) -> None
        self.entries = entries if entries is not None else []

    def __len__(self):
        # type: () -> int
        return len(self.entries)

    def __iter__(self):
        return iter(self.entries)

    def __repr__(self):
        # type: () -> str
        return "LeaderboardResult(entries={0!r})".format(len(self.entries))


class TableMapping(object):
    """One VPIN table mapping, as the server sent it. ``key_type`` and ``table_key`` are opaque."""

    __slots__ = ("key_type", "table_key", "venue_machine")

    def __init__(self, key_type="", table_key="", venue_machine=""):
        # type: (str, str, str) -> None
        self.key_type = key_type
        self.table_key = table_key
        self.venue_machine = venue_machine

    def __repr__(self):
        # type: () -> str
        return "TableMapping(key_type={!r}, table_key={!r}, venue_machine={!r})".format(
            self.key_type, self.table_key, self.venue_machine
        )


class TableMappings(object):
    """A copy of the VPIN table mappings; see :meth:`GameState.table_mappings`."""

    __slots__ = ("result", "etag", "server_time", "mappings")

    def __init__(self, result=0, etag="", server_time="", mappings=None):
        # type: (int, str, str, list | None) -> None
        self.result = result
        self.etag = etag
        self.server_time = server_time
        self.mappings = mappings if mappings is not None else []

    def __len__(self):
        # type: () -> int
        return len(self.mappings)

    def __iter__(self):
        return iter(self.mappings)

    def __repr__(self):
        # type: () -> str
        return "TableMappings(result={!r}, mappings={!r})".format(self.result, len(self.mappings))


class SignatureVerdict(object):
    """The server's signature verdict; its strings are opaque to the SDK."""

    __slots__ = ("status", "accepted_version", "changed_sections", "changed_procedures")

    def __init__(self, status="", accepted_version="", changed_sections=None,
                 changed_procedures=None):
        # type: (str, str, object, object) -> None
        self.status = status
        self.accepted_version = accepted_version
        self.changed_sections = changed_sections if changed_sections is not None else []
        self.changed_procedures = changed_procedures if changed_procedures is not None else []

    def __repr__(self):
        # type: () -> str
        return "SignatureVerdict(status={0!r}, accepted_version={1!r})".format(
            self.status, self.accepted_version
        )


class TableMatch(object):
    """A copy of the answer to :meth:`GameState.match_table_mapping`.

    The strings are ``""`` when absent; the mapping fields are filled only for
    ``TableMatchResult.Mapped``, ``error_code`` and ``error_attr`` only for an
    answer other than a 200. ``http_status`` is 0 when there was no response.
    """

    __slots__ = (
        "http_status",
        "error_code",
        "error_attr",
        "key_type",
        "table_key",
        "mapping_id",
        "venue_machine",
        "venue_machine_name",
        "machine",
        "machine_name",
        "verdict",
    )

    def __init__(self, http_status=0, error_code="", error_attr="", key_type="", table_key="",
                 mapping_id="", venue_machine="", venue_machine_name="", machine="",
                 machine_name="", verdict=None):
        # type: (int, str, str, str, str, str, str, str, str, str, object) -> None
        self.http_status = http_status
        self.error_code = error_code
        self.error_attr = error_attr
        self.key_type = key_type
        self.table_key = table_key
        self.mapping_id = mapping_id
        self.venue_machine = venue_machine
        self.venue_machine_name = venue_machine_name
        self.machine = machine
        self.machine_name = machine_name
        self.verdict = verdict

    def __repr__(self):
        # type: () -> str
        return "TableMatch(http_status={0!r}, venue_machine={1!r}, error_code={2!r})".format(
            self.http_status, self.venue_machine, self.error_code
        )
