#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
using namespace std;

#include <fstream>

/**
 * 【入力内容】
 * N：野生のポケモンの数
 *
 * 1匹目：	H[1]（体力）
 *			C[1]（捕獲可能HP）
 *			V[1]（経験値）
 *			A[1][0] B[1][0]（1つ目の技の威力と回数）
 *			A[1][1] B[1][1]（2つ目の技の威力と回数）
 * ～
 * i匹目：	H[i]（体力）
 *			C[i]（捕獲可能HP）
 *			V[i]（経験値）
 *			A[i][0] B[i][0]（1つ目の技の威力と回数）
 *			A[i][1] B[i][1]（2つ目の技の威力と回数）
 *
 * 最初の手持ちのポケモン：	A[0][0] B[0][0]（1つ目の技の威力と回数）
 *							A[0][1] B[0][1]（2つ目の技の威力と回数）
 */

 /**
  * ポケモンの静的な情報を管理する構造体
  */
struct StaticPokemonData
{
	int maxHp = 0;					// 最大体力
	int captureHp = 0;				// 捕獲可能HP
	int exp = 0;					// 経験値
	int movePower[2] = { 0,0 };		// 技の威力
	int maxMoveCount[2] = { 0,0 };	// 技の最大回数
	int totalDamage = 0;			// そのポケモンが持つ全技の合計ダメージ
	float score = 0.0f;				// 最終スコア（倒すスコア - 捕まえるスコア）
};
vector<StaticPokemonData> g_baseData;

/**
 * ポケモンの現在の居場所を定義
 */
enum class Location : uint8_t
{
	WILD = 0,		// 野生
	HAND,			// 手持ち
	BOX,			// ボックス
	FAINTED,		// ひんし
	STOCKED			// C以下に削られ捕獲待ちの野生
};

/**
 * ポケモンの動的な情報を管理する構造体
 */
struct DynamicPokemonData
{
	int currentHp = 0;						// 現在の体力
	int remainingMoveCount[2] = { 0,0 };	// 現在の技の残り回数
	Location location = Location::WILD;		// 現在の居場所
};

/**
 * @brief ポケモンの情報から、全技の合計ダメージとスコアを事前に計算しておく関数
 * @details
 *	倒すスコア     = exp / maxHp
 *	捕まえるスコア = totalDamage / requiredHp  (requiredHp = maxHp - captureHp)
 *	最終スコア     = 倒すスコア - 捕まえるスコア
 *	スコアが大きいほど「倒す優先」、小さいほど「捕まえる優先」
 */
void InitializePrecalc(StaticPokemonData& p)
{
	// 全技の合計ダメージ
	p.totalDamage = (p.movePower[0] * p.maxMoveCount[0]) + (p.movePower[1] * p.maxMoveCount[1]);
	// 倒すスコア（正規化：最大 5000/500=10.0）
	float defeatScore = static_cast<float>(p.exp) / p.maxHp / 10.0f;
	// 捕まえるスコア（正規化：最大 3750-375=3375）
	// totalDamage - catchCost が大きいほど捕まえる価値が高い
	float catchScore = static_cast<float>(p.totalDamage - (p.maxHp - p.captureHp)) / 3375.0f;
	// 最終スコア（大きいほど「倒す優先」、小さいほど「捕まえる優先」）
	p.score = defeatScore - catchScore;
}

/**
 * @brief  手持ちポケモン全員の技から、倒すために最大ダメージの技を選ぶ関数
 * @return { attackerId, moveIndex }　使える技がなければ { -1, -1 }
 */
pair<int, int> SelectBestAttackMove(
	const vector<int>& handIds,
	const vector<DynamicPokemonData>& pokemons)
{
	int bestDamage = -1;
	int bestAttacker = -1;
	int bestMove = -1;

	for (int handId : handIds) {
		for (int m = 0; m < 2; ++m) {
			if (pokemons[handId].remainingMoveCount[m] > 0 && g_baseData[handId].movePower[m] > bestDamage)
			{
				bestDamage = g_baseData[handId].movePower[m];
				bestAttacker = handId;
				bestMove = m;
			}
		}
	}

	return { bestAttacker, bestMove };
}

/**
 * @brief 捕獲圏内まで削り切るための技の使用順を計画する関数
 * @details
 *	currentHp を起点にDPで到達可能なHPと、そこに至る技を記録する。
 *	捕獲可能範囲 [1, captureHp] の中で最もCに近い（無駄が少ない）HPへの
 *	技の使用順リストを返す。
 *	空リストを返した場合は削り切れない。
 * @return {attackerId, moveIndex} のリスト（先頭から順に実行する）
 */
vector<pair<int, int>> PlanCatchSequence(
	const vector<int>& handIds,
	const vector<DynamicPokemonData>& pokemons,
	int targetId)
{
	int currentHp = pokemons[targetId].currentHp;
	int captureHp = g_baseData[targetId].captureHp;

	// すでにC以下なら手順不要
	if (currentHp >= 1 && currentHp <= captureHp) return {};

	// DP：各HPに到達するための「最後に使った技」を記録
	// prev[hp] = {attackerId, moveIndex}（-1,-1なら未到達）
	vector<pair<int, int>> prev(currentHp + 1, { -1, -1 });
	vector<bool> reachable(currentHp + 1, false);
	reachable[currentHp] = true;

	for (int h : handIds) {
		for (int m = 0; m < 2; ++m) {
			int power = g_baseData[h].movePower[m];
			int count = pokemons[h].remainingMoveCount[m];
			if (count == 0) continue;

			for (int k = 0; k < count; ++k)
			{
				// コピーを使って連鎖加算を防ぐ
				vector<bool> next = reachable;
				for (int hp = power + 1; hp <= currentHp; ++hp)
				{
					if (reachable[hp] && !next[hp - power])
					{
						next[hp - power] = true;
						prev[hp - power] = { h, m };
					}
				}
				reachable = next;
			}
		}
	}

	// captureHp以下で最もCに近い（hpAfterが最大）到達点を探す
	int bestHp = -1;
	for (int hp = captureHp; hp >= 1; --hp)
	{
		if (reachable[hp])
		{
			bestHp = hp;
			break;
		}
	}

	if (bestHp == -1) return {}; // 削り切れない

	// bestHpに至るまでの技の使用順を逆順に復元
	vector<pair<int, int>> plan;
	int hp = bestHp;
	while (hp != currentHp)
	{
		auto [attackerId, moveIndex] = prev[hp];
		plan.push_back({ attackerId, moveIndex });
		hp += g_baseData[attackerId].movePower[moveIndex];
	}

	// 逆順になっているので反転
	reverse(plan.begin(), plan.end());
	return plan;
}

/**
 * 手持ちに技が残っているかどうかを確認する関数
 */
bool HasAnyMove(
	const vector<int>& handIds,
	const vector<DynamicPokemonData>& pokemons)
{
	for (int handId : handIds) {
		if (pokemons[handId].remainingMoveCount[0] > 0 ||
			pokemons[handId].remainingMoveCount[1] > 0)
		{
			return true;
		}
	}
	return false;
}

/**
 * @brief 事前計算で倒す候補と捕まえる候補を決定する関数
 */
void DecideStrategy(
	const vector<int>& sortedRank,
	int& defeatCount)
{
	int N = (int)sortedRank.size();
	int initialDamage = g_baseData[0].totalDamage;

	vector<int> defeatCostPrefix(N + 1, 0);
	vector<int> catchCostSuffix(N + 1, 0);
	vector<int> catchBonusSuffix(N + 1, 0);

	for (int i = 0; i < N; ++i) {
		int id = sortedRank[i];
		defeatCostPrefix[i + 1] = defeatCostPrefix[i] + g_baseData[id].maxHp;
	}
	for (int i = N - 1; i >= 0; --i) {
		int id = sortedRank[i];
		catchCostSuffix[i] = catchCostSuffix[i + 1] + (g_baseData[id].maxHp - g_baseData[id].captureHp);
		catchBonusSuffix[i] = catchBonusSuffix[i + 1] + g_baseData[id].totalDamage;
	}

	defeatCount = 0;
	for (int k = 0; k <= N; ++k)
	{
		int availableDamage = initialDamage + catchBonusSuffix[k];
		int requiredDamage = defeatCostPrefix[k] + catchCostSuffix[k];
		if (availableDamage >= requiredDamage)
			defeatCount = k;
		else
			break;
	}
}


/******************************************************/


int main()
{
	// 提出時、以下はコメントアウト
	/** ここから */
	for (int fileIdx = 0; fileIdx <= 4; fileIdx++)
	{
		char inputPath[64];
		char outputPath[64];
		sprintf(inputPath, "入力ファイル/%04d.txt", fileIdx);
		sprintf(outputPath, "出力ファイル/%04d.out", fileIdx);
		ifstream is(inputPath);
		ofstream os(outputPath);
		streambuf* oldIn = cin.rdbuf(is.rdbuf());
		streambuf* oldOut = cout.rdbuf(os.rdbuf());
		/** ここまで */

		////////////////////////////////////////////////////
		// 変数の宣言や、入力の受け取りなどを行うフェーズ //
		////////////////////////////////////////////////////

		int N;
		cin >> N;

		g_baseData.assign(N + 1, StaticPokemonData());

		for (int i = 1; i <= N; i++)
		{
			StaticPokemonData& p = g_baseData[i];
			cin >> p.maxHp >> p.captureHp >> p.exp
				>> p.movePower[0] >> p.maxMoveCount[0] >> p.movePower[1] >> p.maxMoveCount[1];
			InitializePrecalc(p);
		}

		StaticPokemonData& p0 = g_baseData[0];
		cin >> p0.movePower[0] >> p0.maxMoveCount[0] >> p0.movePower[1] >> p0.maxMoveCount[1];
		InitializePrecalc(p0);

		vector<int> sortedRank;
		for (int i = 1; i <= N; i++) sortedRank.push_back(i);
		sort(sortedRank.begin(), sortedRank.end(), [](int a, int b) {
			return g_baseData[a].score > g_baseData[b].score;
			});

		int defeatCount = 0;
		DecideStrategy(sortedRank, defeatCount);

		vector<int> defeatTargets(sortedRank.begin(), sortedRank.begin() + defeatCount);
		vector<int> catchTargets(sortedRank.begin() + defeatCount, sortedRank.end());

		/////////////////////////////
		// 初期状態の構築           //
		/////////////////////////////

		vector<DynamicPokemonData> pokemons(N + 1);
		for (int i = 0; i <= N; i++)
		{
			pokemons[i].currentHp = g_baseData[i].maxHp;
			pokemons[i].remainingMoveCount[0] = g_baseData[i].maxMoveCount[0];
			pokemons[i].remainingMoveCount[1] = g_baseData[i].maxMoveCount[1];
			pokemons[i].location = (i == 0) ? Location::HAND : Location::WILD;
		}

		vector<int> handIds = { 0 };

		bool phase1Complete = catchTargets.empty();

		// フェーズ1の攻撃計画
		// catchPlanTargetId: 現在削っている対象
		// catchPlan: 残りの技の使用順リスト {attackerId, moveIndex}
		int catchPlanTargetId = -1;
		vector<pair<int, int>> catchPlan;


		////////////////////
		// メインループ   //
		////////////////////

		while (true)
		{
			//=============================================================================
			// ステップ1：ストック済みポケモンを捕まえる（ボックス送り前に優先）
			//=============================================================================
			if ((int)handIds.size() < 6)
			{
				vector<pair<int, int>> wildRemaining;
				for (int targetId : catchTargets)
				{
					if (pokemons[targetId].location != Location::WILD) continue;
					wildRemaining.push_back({
						pokemons[targetId].currentHp,
						g_baseData[targetId].captureHp
						});
				}

				int catchId = -1;
				int fallbackId = -1;
				for (int targetId : catchTargets)
				{
					Location loc = pokemons[targetId].location;
					bool isStock = (loc == Location::STOCKED) ||
						(loc == Location::WILD &&
							pokemons[targetId].currentHp >= 1 &&
							pokemons[targetId].currentHp <= g_baseData[targetId].captureHp);
					if (!isStock) continue;

					if (fallbackId == -1) fallbackId = targetId;

					bool useful = wildRemaining.empty();
					for (auto& [wildHp, wildC] : wildRemaining)
					{
						for (int m = 0; m < 2; ++m)
						{
							int power = g_baseData[targetId].movePower[m];
							int hpAfter = wildHp - power;
							if (hpAfter >= 1 && hpAfter <= wildC)
							{
								useful = true;
								break;
							}
						}
						if (useful) break;
					}
					if (useful) { catchId = targetId; break; }
				}

				if (catchId == -1) catchId = fallbackId;

				if (catchId != -1)
				{
					pokemons[catchId].location = Location::HAND;
					handIds.push_back(catchId);
					pokemons[catchId].remainingMoveCount[0] = g_baseData[catchId].maxMoveCount[0];
					pokemons[catchId].remainingMoveCount[1] = g_baseData[catchId].maxMoveCount[1];
					cout << 2 << " " << catchId << "\n";
					continue;
				}
			}

			//=============================================================================
			// ステップ2：技が尽きた手持ちをボックスへ送る
			// catchPlanが空の場合のみ実行（計画実行中は手持ちを維持）
			//=============================================================================
			if (catchPlan.empty())
			{
				vector<int> newHandIds;
				for (int h : handIds)
				{
					if (pokemons[h].remainingMoveCount[0] == 0 &&
						pokemons[h].remainingMoveCount[1] == 0)
					{
						pokemons[h].location = Location::BOX;
						cout << 3 << " " << h << "\n";
					}
					else
					{
						newHandIds.push_back(h);
					}
				}
				handIds = newHandIds;
			}
			if (!HasAnyMove(handIds, pokemons)) break;


			//=============================================================================
			// ステップ3：技を使う行動
			//=============================================================================
			bool acted = false;

			if (!phase1Complete)
			{
				phase1Complete = true;
				for (int targetId : catchTargets) {
					if (pokemons[targetId].location == Location::WILD) {
						phase1Complete = false;
						break;
					}
				}
			}

			if (!phase1Complete)
			{
				//-------------------------------------------------------------------------
				// フェーズ1：catchTargetsをC以下に削ってストックする
				// catchPlanに従って技を使用し、計画通りに削り切る
				//-------------------------------------------------------------------------

				// catchPlanTargetIdが無効になった場合はリセット
				if (catchPlanTargetId != -1 &&
					pokemons[catchPlanTargetId].location != Location::WILD)
				{
					catchPlanTargetId = -1;
					catchPlan.clear();
				}

				// catchPlanが空なら新しい対象を探して計画を立てる
				if (catchPlan.empty())
				{
					for (int targetId : catchTargets)
					{
						if (pokemons[targetId].location != Location::WILD) continue;
						auto plan = PlanCatchSequence(handIds, pokemons, targetId);
						if (!plan.empty())
						{
							catchPlanTargetId = targetId;
							catchPlan = plan;
							break;
						}
					}
				}

				// catchPlanに従って攻撃
				if (!catchPlan.empty())
				{
					auto [attackerId, moveIndex] = catchPlan.front();
					catchPlan.erase(catchPlan.begin());

					int attackedTargetId = catchPlanTargetId;
					int damage = g_baseData[attackerId].movePower[moveIndex];
					pokemons[catchPlanTargetId].currentHp -= damage;
					pokemons[attackerId].remainingMoveCount[moveIndex]--;

					if (pokemons[catchPlanTargetId].currentHp <= 0)
					{
						pokemons[catchPlanTargetId].location = Location::FAINTED;
						catchPlanTargetId = -1;
						catchPlan.clear();
					}
					else if (pokemons[catchPlanTargetId].currentHp <= g_baseData[catchPlanTargetId].captureHp)
					{
						pokemons[catchPlanTargetId].location = Location::STOCKED;
						catchPlanTargetId = -1;
						catchPlan.clear();
					}

					cout << 1 << " " << attackerId << " " << attackedTargetId << " " << (moveIndex + 1) << "\n";
					acted = true;
				}

				// フェーズ1で計画が立てられない場合はdefeatTargetsを攻撃して手持ちを入れ替える
				if (!acted)
				{
					for (int targetId : defeatTargets)
					{
						if (pokemons[targetId].location != Location::WILD) continue;
						auto [attackerId, moveIndex] = SelectBestAttackMove(handIds, pokemons);
						if (attackerId != -1)
						{
							int damage = g_baseData[attackerId].movePower[moveIndex];
							pokemons[targetId].currentHp -= damage;
							pokemons[attackerId].remainingMoveCount[moveIndex]--;
							if (pokemons[targetId].currentHp <= 0)
								pokemons[targetId].location = Location::FAINTED;
							cout << 1 << " " << attackerId << " " << targetId << " " << (moveIndex + 1) << "\n";
							acted = true;
						}
						break;
					}
				}
			}

			if (!acted && phase1Complete)
			{
				//-------------------------------------------------------------------------
				// フェーズ2：ストックを捕まえながらdefeatTargetsを倒す
				//-------------------------------------------------------------------------
				for (int targetId : defeatTargets)
				{
					if (pokemons[targetId].location != Location::WILD) continue;
					auto [attackerId, moveIndex] = SelectBestAttackMove(handIds, pokemons);
					if (attackerId != -1)
					{
						int damage = g_baseData[attackerId].movePower[moveIndex];
						pokemons[targetId].currentHp -= damage;
						pokemons[attackerId].remainingMoveCount[moveIndex]--;
						if (pokemons[targetId].currentHp <= 0)
							pokemons[targetId].location = Location::FAINTED;
						cout << 1 << " " << attackerId << " " << targetId << " " << (moveIndex + 1) << "\n";
						acted = true;
					}
					break;
				}
			}

			if (!acted) break;
		}

		// 提出時、以下はコメントアウト
		/** ここから */
		cin.rdbuf(oldIn);
		cout.rdbuf(oldOut);
	}
	/** ここまで */

	return 0;
}