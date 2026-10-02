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


class AchievementRule(object):
    """One rule of an achievement; all rules must be satisfied together."""

    __slots__ = ("type", "comparison", "target", "reference")

    def __init__(self, type=0, comparison=2, target=0, reference=""):
        # type: (int, int, int, str) -> None
        self.type = type
        self.comparison = comparison
        self.target = target
        self.reference = reference

    def __repr__(self):
        # type: () -> str
        return "AchievementRule(type={!r}, comparison={!r}, target={!r}, reference={!r})".format(
            self.type, self.comparison, self.target, self.reference
        )


class Achievement(object):
    """An achievement definition with its full rule set."""

    __slots__ = (
        "key",
        "name",
        "description",
        "scope",
        "evaluation",
        "is_trophy",
        "is_badge",
        "visible",
        "obscure",
        "notify_when_achieved",
        "icon_url",
        "obscure_image_url",
        "group_id",
        "level",
        "display_position",
        "rules",
    )

    def __init__(self):
        # type: () -> None
        self.key = ""
        self.name = ""
        self.description = ""
        self.scope = 0
        self.evaluation = 0
        self.is_trophy = False
        self.is_badge = False
        self.visible = True
        self.obscure = False
        self.notify_when_achieved = False
        self.icon_url = ""
        self.obscure_image_url = ""
        self.group_id = None  # type: int | None
        self.level = None  # type: int | None
        self.display_position = None  # type: int | None
        self.rules = []  # type: list

    def __repr__(self):
        # type: () -> str
        return "Achievement(key={!r}, rules={!r})".format(self.key, len(self.rules))


class AchievementRuleProgress(object):
    """A claimed player's progress on one rule."""

    __slots__ = ("judged", "value", "target", "satisfied")

    def __init__(self, judged=False, value=0, target=0, satisfied=False):
        # type: (bool, int, int, bool) -> None
        self.judged = judged
        self.value = value
        self.target = target
        self.satisfied = satisfied

    def __repr__(self):
        # type: () -> str
        return "AchievementRuleProgress(value={!r}, target={!r}, satisfied={!r})".format(
            self.value, self.target, self.satisfied
        )


class AchievementProgress(object):
    """A claimed player's progress on one achievement during the current game."""

    __slots__ = ("held", "all_satisfied", "confirmed", "rules")

    def __init__(self, held=False, all_satisfied=False, confirmed=False, rules=None):
        # type: (bool, bool, bool, list | None) -> None
        self.held = held
        self.all_satisfied = all_satisfied
        self.confirmed = confirmed
        self.rules = rules if rules is not None else []

    def __repr__(self):
        # type: () -> str
        return "AchievementProgress(held={!r}, all_satisfied={!r})".format(
            self.held, self.all_satisfied
        )


class AchievementUpdate(object):
    """Content of an ``AchievementUpdated`` event.

    Attributes:
        rules: ``(index, value, satisfied)`` tuples for the rules the machine
            could judge.
    """

    __slots__ = ("key", "player", "user_id", "status", "rules")

    def __init__(self, key="", player=0, user_id="", status=0, rules=None):
        # type: (str, int, str, int, list | None) -> None
        self.key = key
        self.player = player
        self.user_id = user_id
        self.status = status
        self.rules = rules if rules is not None else []

    def __repr__(self):
        # type: () -> str
        return "AchievementUpdate(key={!r}, player={!r}, status={!r})".format(
            self.key, self.player, self.status
        )
