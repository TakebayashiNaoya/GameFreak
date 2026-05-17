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
 * @brief 手持ちの技の合計残りダメージを計算する関数
 */
int CalcTotalRemainingDamage(
	const vector<int>& handIds,
	const vector<DynamicPokemonData>& pokemons)
{
	int total = 0;

	for (int h : handIds) {
		for (int m = 0; m < 2; ++m) {
			total += g_baseData[h].movePower[m] * pokemons[h].remainingMoveCount[m];
		}
	}
	return total;
}

/**
 * @brief  手持ちポケモン全員の技から、倒すために最大ダメージの技を選ぶ関数
 * @return { attackerId, moveIndex }　使える技がなければ { -1, -1 }
 */
pair<int, int> SelectBestAttackMove(
	const vector<int>& handIds,
	const vector<DynamicPokemonData>& pokemons)
{
	// 最大ダメージ
	int bestDamage = -1;
	// 最適なアタッカーポケモンのインデックス
	int bestAttacker = -1;
	// 最適な技のインデックス
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

// 到達可能なHPを管理する固定サイズbool配列
// 対象ポケモンのcurrentHp（最大1000）を起点に減算方向でDPする
// vector<bool>の動的確保を避けることで毎ターンの処理を高速化する
static const int MAX_HP = 1000;
static bool g_reachable[MAX_HP + 1];
static bool g_prev[MAX_HP + 1];

/**
 * @brief 手持ちの技の組み合わせで対象をCまで削り切れるか判定する関数
 * @details
 *	currentHp を起点に、各技を 0〜残り回数 の範囲で使った場合に
 *	到達可能なHPの集合をDPで管理する。
 *	固定サイズの配列を使いまわすことで動的メモリ確保コストをなくす。
 *	捕獲可能範囲 [1, captureHp] に到達できれば true を返す。
 * @return 削り切れる場合true、不可能な場合false
 */
bool CanReduceToCaptureRange(
	const vector<int>& handIds,
	const vector<DynamicPokemonData>& pokemons,
	int targetId)
{
	int currentHp = pokemons[targetId].currentHp;
	int captureHp = g_baseData[targetId].captureHp;

	// すでにC以下なら削る必要なし
	if (currentHp >= 1 && currentHp <= captureHp) return true;

	// currentHp を起点に減算方向でDP
	fill(g_reachable, g_reachable + currentHp + 1, false);
	g_reachable[currentHp] = true;

	for (int h : handIds) {
		for (int m = 0; m < 2; ++m) {
			int power = g_baseData[h].movePower[m];
			int count = pokemons[h].remainingMoveCount[m];
			if (count == 0) continue;

			// この技をk回(1〜count)使う場合をDPに追加
			// コピーを使って連鎖加算を防ぐ
			for (int k = 0; k < count; ++k)
			{
				memcpy(g_prev, g_reachable, (currentHp + 1) * sizeof(bool));
				for (int hp = power + 1; hp <= currentHp; ++hp)
				{
					if (g_prev[hp])
						g_reachable[hp - power] = true;
				}
			}
		}
	}

	// captureHp以下かつ1以上のHPに到達可能か確認
	for (int hp = 1; hp <= captureHp; ++hp)
	{
		if (g_reachable[hp]) return true;
	}
	return false;
}

/**
 * @brief 手持ちポケモン全員の技から、捕まえるために最善の技を選ぶ関数
 * @details
 * 優先1：撃った後のHPが 1 以上 captureHp 以下に収まる技（captureHpに最も近いもの）
 * 優先2：優先1がなければ、captureHp より大きく残す技（最大ダメージ）
 * @return { attackerId, moveIndex }　使える技がなければ { -1, -1 }
 */
pair<int, int> SelectBestCatchMove(
	const vector<int>& handIds,
	const vector<DynamicPokemonData>& pokemons,
	int targetId)
{
	int currentHp = pokemons[targetId].currentHp;
	int captureHp = g_baseData[targetId].captureHp;

	// 優先1：1発で captureHp 以下に収める技（hpAfter が captureHp に最も近いもの）
	int best1Attacker = -1, best1Move = -1, best1HpAfter = -1;

	// 優先2：captureHp より大きいまま残す技（最大ダメージ）
	// CanReduceToCaptureRange で削り切れることは呼び出し元で確認済みのため、
	// ここでは再チェックを行わず最大ダメージの技を選ぶ
	int best2Attacker = -1, best2Move = -1, best2Damage = -1;

	for (int handId : handIds) {
		for (int m = 0; m < 2; ++m) {
			if (pokemons[handId].remainingMoveCount[m] > 0)
			{
				int damage = g_baseData[handId].movePower[m];
				int hpAfter = currentHp - damage;

				// 優先1：captureHp 以下に収まる（hpAfter が大きいほど良い）
				if (hpAfter >= 1 && hpAfter <= captureHp && hpAfter > best1HpAfter)
				{
					best1HpAfter = hpAfter;
					best1Attacker = handId;
					best1Move = m;
				}
				// 優先2：まだ captureHp より大きいが、できるだけ削る
				else if (hpAfter > captureHp && damage > best2Damage)
				{
					best2Damage = damage;
					best2Attacker = handId;
					best2Move = m;
				}
			}
		}
	}

	if (best1Attacker != -1) return { best1Attacker, best1Move };
	if (best2Attacker != -1) return { best2Attacker, best2Move };
	return { -1, -1 };
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
 * @details
 *	sortedRankの前からk匹を「倒す」、残りを「捕まえる」と仮定し、
 *	使える合計ダメージ ≥ 倒すコスト + 捕まえるコスト を満たす最大のkを探す。
 *	使える合計ダメージ = 初期手持ちの合計ダメージ + 捕まえるポケモンの合計ダメージ
 * @param sortedRank  スコア降順のインデックス列
 * @param defeatCount 決定した「倒す」匹数（出力）
 */
void DecideStrategy(
	const vector<int>& sortedRank,
	int& defeatCount)
{
	int N = (int)sortedRank.size();

	// 初期手持ちの合計ダメージ
	int initialDamage = g_baseData[0].totalDamage;

	// 累積和を事前計算
	vector<int> defeatCostPrefix(N + 1, 0);	// 前からk匹倒すコストの累積和
	vector<int> catchCostSuffix(N + 1, 0);	// 後ろからN-k匹捕まえるコストの累積和
	vector<int> catchBonusSuffix(N + 1, 0);	// 後ろからN-k匹捕まえるボーナスの累積和

	for (int i = 0; i < N; ++i) {
		int id = sortedRank[i];
		defeatCostPrefix[i + 1] = defeatCostPrefix[i] + g_baseData[id].maxHp;
	}
	for (int i = N - 1; i >= 0; --i) {
		int id = sortedRank[i];
		catchCostSuffix[i] = catchCostSuffix[i + 1] + (g_baseData[id].maxHp - g_baseData[id].captureHp);
		catchBonusSuffix[i] = catchBonusSuffix[i + 1] + g_baseData[id].totalDamage;
	}

	// k=0からNまで順に条件を確認し、満たす最大のkを探す
	defeatCount = 0;
	for (int k = 0; k <= N; ++k)
	{
		int availableDamage = initialDamage + catchBonusSuffix[k];
		int requiredDamage = defeatCostPrefix[k] + catchCostSuffix[k];

		if (availableDamage >= requiredDamage)
		{
			defeatCount = k;
		}
		else
		{
			// 条件を満たさなくなった時点で終了
			break;
		}
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
		// cin/cout のバッファを差し替える
		streambuf* oldIn = cin.rdbuf(is.rdbuf());
		streambuf* oldOut = cout.rdbuf(os.rdbuf());
		/** ここまで */

		////////////////////////////////////////////////////
		// 変数の宣言や、入力の受け取りなどを行うフェーズ //
		////////////////////////////////////////////////////

		// 野生のポケモンの数を読み込み
		int N;
		cin >> N;

		// 静的データ（g_baseData）のメモリ確保
		// 0番：最初の手持ち、1～N番：野生のポケモン
		g_baseData.assign(N + 1, StaticPokemonData());

		// 野生のポケモンの情報を読み込み
		for (int i = 1; i <= N; i++)
		{
			StaticPokemonData& p = g_baseData[i];
			cin >> p.maxHp >> p.captureHp >> p.exp
				>> p.movePower[0] >> p.maxMoveCount[0] >> p.movePower[1] >> p.maxMoveCount[1];

			// スコア計算用の事前処理
			InitializePrecalc(p);
		}

		// 最初の手持ちポケモンの情報を読み込み
		StaticPokemonData& p0 = g_baseData[0];
		cin >> p0.movePower[0] >> p0.maxMoveCount[0] >> p0.movePower[1] >> p0.maxMoveCount[1];
		InitializePrecalc(p0);

		// スコアの降順でソートしたインデックス列を作成
		// 先頭が「倒す優先」、末尾が「捕まえる優先」
		vector<int> sortedRank;
		for (int i = 1; i <= N; i++) {
			sortedRank.push_back(i);
		}
		sort(sortedRank.begin(), sortedRank.end(), [](int a, int b) {
			return g_baseData[a].score > g_baseData[b].score;
			});

		// 事前計算：倒す匹数を決定
		int defeatCount = 0;
		DecideStrategy(sortedRank, defeatCount);

		// 倒す候補・捕まえる候補のセットを構築
		// defeatTargets：スコア上位defeatCount匹（倒す）
		// catchTargets ：残りのN-defeatCount匹（捕まえる）
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

		// 手持ちリスト（インデックスのみ）
		vector<int> handIds = { 0 };

		// フェーズ管理
		// phase1: catchTargetsをC以下に削ってストックする
		// phase2: ストックを捕まえながらdefeatTargetsを倒す
		bool phase1Complete = catchTargets.empty();


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
				// 残っている野生catchTargetのhpとcaptureHpを収集
				// （有効な技を持つSTOCKEDを優先するための情報）
				vector<pair<int, int>> wildRemaining; // {currentHp, captureHp}
				for (int targetId : catchTargets)
				{
					if (pokemons[targetId].location != Location::WILD) continue;
					wildRemaining.push_back({
						pokemons[targetId].currentHp,
						g_baseData[targetId].captureHp
						});
				}

				// 有効な技を持つSTOCKEDを優先、なければ任意のSTOCKEDを捕まえる
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

					// このポケモンの技が残野生catchTargetに対して有効かチェック
					// 有効 = 1発でC以下に収められる威力を持つ
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
					if (useful)
					{
						catchId = targetId;
						break;
					}
				}

				// 有効なSTOCKEDがなければ任意のSTOCKEDを捕まえる
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
			//=============================================================================
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

			// フェーズ1完了チェック：全catchTargetsがWILD以外になれば完了
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
				// CanReduceToCaptureRangeで削り切れる対象が見つかった時点で攻撃・break
				//-------------------------------------------------------------------------
				for (int targetId : catchTargets)
				{
					if (pokemons[targetId].location != Location::WILD) continue;

					// DPで実際にCまで削り切れるか確認
					if (!CanReduceToCaptureRange(handIds, pokemons, targetId)) continue;

					// 削れる技を選ぶ
					auto [attackerId, moveIndex] = SelectBestCatchMove(handIds, pokemons, targetId);
					if (attackerId != -1)
					{
						int damage = g_baseData[attackerId].movePower[moveIndex];
						pokemons[targetId].currentHp -= damage;
						pokemons[attackerId].remainingMoveCount[moveIndex]--;

						if (pokemons[targetId].currentHp <= 0)
						{
							// 誤って倒してしまった場合はひんしに
							pokemons[targetId].location = Location::FAINTED;
						}
						else if (pokemons[targetId].currentHp <= g_baseData[targetId].captureHp)
						{
							// C以下になったのでストック
							pokemons[targetId].location = Location::STOCKED;
						}

						cout << 1 << " " << attackerId << " " << targetId << " " << (moveIndex + 1) << "\n";
						acted = true;
						break;
					}
				}

				// フェーズ1で詰まった場合はdefeatTargetsを攻撃して手持ちを入れ替える
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