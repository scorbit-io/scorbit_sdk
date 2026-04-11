# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Shared data types used across the wrapper."""


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
            return "PlayerInfo(id={!r}, preferred_name={!r})".format(
                self.id, self.preferred_name
            )
        return "PlayerInfo(claim_deeplink={!r})".format(self.claim_deeplink)
